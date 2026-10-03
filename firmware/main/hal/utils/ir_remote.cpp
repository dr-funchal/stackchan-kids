/*
 * SPDX-License-Identifier: MIT
 */
#include "ir_remote.h"
#include "sd_card.h"
#include "sd_features.h"
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>

static const char* TAG = "IrRemote";

static constexpr gpio_num_t kTxPin        = GPIO_NUM_5;
static constexpr gpio_num_t kRxPin        = GPIO_NUM_10;
static constexpr uint32_t kResolutionHz   = 1000000;  // 1 tick = 1 us
static constexpr uint32_t kCarrierHz      = 38000;
static constexpr size_t kRxSymbols        = 192;      // All four RX memory blocks: enough for A/C remotes
static constexpr uint32_t kFrameEndNs     = 20 * 1000 * 1000;  // Silence that ends a button press
static constexpr uint32_t kGlitchNs       = 1250;
static constexpr int kMinDurations        = 8;        // Shorter captures are noise

static std::mutex _ir_mutex;  // One learn or send at a time

static std::string file_for(const std::string& name)
{
    return std::string(ir_remote::kDir) + "/" + name + ".txt";
}

static void ensure_dir()
{
    sd_card::BusGuard guard;
    mkdir(ir_remote::kDir, 0775);
}

/* --------------------------------- Learn --------------------------------- */

static bool IRAM_ATTR on_rx_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata, void* user)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(static_cast<QueueHandle_t>(user), edata, &woken);
    return woken == pdTRUE;
}

bool ir_remote::learn(const std::string& rawName, int timeoutMs, std::string& error)
{
    std::string name = sd_paths::sanitize(rawName);
    if (!sd_card::isMounted() || name.empty()) {
        error = "SD card missing or invalid name";
        return false;
    }
    std::lock_guard<std::mutex> lock(_ir_mutex);

    rmt_rx_channel_config_t rx_config = {};
    rx_config.gpio_num                = kRxPin;
    rx_config.clk_src                 = RMT_CLK_SRC_DEFAULT;
    rx_config.resolution_hz           = kResolutionHz;
    rx_config.mem_block_symbols       = kRxSymbols;

    rmt_channel_handle_t rx = nullptr;
    if (rmt_new_rx_channel(&rx_config, &rx) != ESP_OK) {
        error = "IR receiver unavailable";
        return false;
    }

    QueueHandle_t queue          = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done             = on_rx_done;
    rmt_rx_register_event_callbacks(rx, &cbs, queue);
    rmt_enable(rx);

    auto* symbols = (rmt_symbol_word_t*)heap_caps_malloc(kRxSymbols * sizeof(rmt_symbol_word_t), MALLOC_CAP_INTERNAL);
    rmt_receive_config_t receive_config = {};
    receive_config.signal_range_min_ns  = kGlitchNs;
    receive_config.signal_range_max_ns  = kFrameEndNs;

    std::vector<uint32_t> durations;
    int64_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
    ESP_LOGI(TAG, "Learning '%s': waiting for a remote button", name.c_str());

    // Keep listening until a capture long enough to be a real button arrives (ignores stray blips)
    while (symbols && (int64_t)xTaskGetTickCount() < deadline) {
        if (rmt_receive(rx, symbols, kRxSymbols * sizeof(rmt_symbol_word_t), &receive_config) != ESP_OK) {
            break;
        }
        rmt_rx_done_event_data_t done = {};
        TickType_t wait               = std::max<int64_t>(1, deadline - (int64_t)xTaskGetTickCount());
        if (xQueueReceive(queue, &done, wait) != pdTRUE) {
            break;  // Timed out; rmt_disable below aborts the pending receive
        }

        // The receiver output is active low: level 0 = carrier present (mark)
        durations.clear();
        for (size_t i = 0; i < done.num_symbols; i++) {
            const auto& s = done.received_symbols[i];
            if (s.duration0) {
                durations.push_back(s.duration0);
            }
            if (s.duration1) {
                durations.push_back(s.duration1);
            }
        }
        if ((int)durations.size() >= kMinDurations && done.received_symbols[0].level0 == 0) {
            break;
        }
        durations.clear();
    }

    rmt_disable(rx);
    rmt_del_channel(rx);
    vQueueDelete(queue);
    heap_caps_free(symbols);

    if (durations.empty()) {
        error = "No remote button received in time";
        return false;
    }
    // A capture must end on a mark: drop the trailing space if any
    if (durations.size() % 2 == 0) {
        durations.pop_back();
    }

    ensure_dir();
    sd_card::BusGuard guard;
    FILE* f = fopen(file_for(name).c_str(), "w");
    if (!f) {
        error = "Cannot write to the SD card";
        return false;
    }
    fprintf(f, "%u\n", (unsigned)kCarrierHz);
    for (size_t i = 0; i < durations.size(); i++) {
        fprintf(f, i ? ",%u" : "%u", (unsigned)durations[i]);
    }
    fputc('\n', f);
    fclose(f);

    ESP_LOGI(TAG, "Learned '%s' (%u durations)", name.c_str(), (unsigned)durations.size());
    return true;
}

/* ---------------------------------- Send --------------------------------- */

static bool load(const std::string& name, uint32_t& carrier, std::vector<uint32_t>& durations)
{
    sd_card::BusGuard guard;
    FILE* f = fopen(file_for(name).c_str(), "r");
    if (!f) {
        return false;
    }
    char line[32];
    carrier = fgets(line, sizeof(line), f) ? strtoul(line, nullptr, 10) : kCarrierHz;
    unsigned value;
    while (fscanf(f, "%u", &value) == 1) {
        durations.push_back(value);
        fgetc(f);  // Skip the comma
    }
    fclose(f);
    return !durations.empty();
}

bool ir_remote::send(const std::string& rawName, std::string& error)
{
    // Accept the exact name or a loose match ("ligar tv" -> "tv_ligar")
    std::string wanted = sd_paths::sanitize(rawName);
    std::string name;
    for (auto& button : list()) {
        if (button == wanted) {
            name = button;
            break;
        }
        if (name.empty() && !wanted.empty() &&
            (button.find(wanted) != std::string::npos || wanted.find(button) != std::string::npos)) {
            name = button;
        }
    }

    uint32_t carrier = kCarrierHz;
    std::vector<uint32_t> durations;
    if (name.empty() || !load(name, carrier, durations)) {
        error = "Button not learned yet";
        return false;
    }
    std::lock_guard<std::mutex> lock(_ir_mutex);

    // Marks carry the 38 kHz carrier (level 1), spaces are idle (level 0). RMT halves hold up to 32767 ticks
    std::vector<rmt_symbol_word_t> symbols;
    auto push = [&symbols](uint32_t mark, uint32_t space) {
        while (mark > 32767 || space > 32767) {
            uint32_t m = std::min<uint32_t>(mark, 32767);
            uint32_t s = mark > 32767 ? 0 : std::min<uint32_t>(space, 32767);
            rmt_symbol_word_t sym = {};
            sym.duration0         = m;
            sym.level0            = m ? 1 : 0;
            sym.duration1         = s;
            sym.level1            = 0;
            symbols.push_back(sym);
            mark -= m;
            space -= s;
        }
        rmt_symbol_word_t sym = {};
        sym.duration0         = mark;
        sym.level0            = mark ? 1 : 0;
        sym.duration1         = space;
        sym.level1            = 0;
        symbols.push_back(sym);
    };
    for (size_t i = 0; i < durations.size(); i += 2) {
        push(durations[i], i + 1 < durations.size() ? durations[i + 1] : 0);
    }

    rmt_tx_channel_config_t tx_config = {};
    tx_config.gpio_num                = kTxPin;
    tx_config.clk_src                 = RMT_CLK_SRC_DEFAULT;
    tx_config.resolution_hz           = kResolutionHz;
    tx_config.mem_block_symbols       = 48;
    tx_config.trans_queue_depth       = 2;

    rmt_channel_handle_t tx = nullptr;
    if (rmt_new_tx_channel(&tx_config, &tx) != ESP_OK) {
        error = "IR transmitter unavailable";
        return false;
    }
    rmt_carrier_config_t carrier_config = {};
    carrier_config.frequency_hz         = carrier ? carrier : kCarrierHz;
    carrier_config.duty_cycle           = 0.5f;  // Longer LED pulses reach further than the usual 33%
    rmt_apply_carrier(tx, &carrier_config);

    rmt_encoder_handle_t encoder        = nullptr;
    rmt_copy_encoder_config_t enc_config = {};
    rmt_new_copy_encoder(&enc_config, &encoder);
    rmt_enable(tx);

    // Send three times with a short gap: a weak or partly blocked first frame is common across a room
    rmt_transmit_config_t transmit_config = {};
    esp_err_t err                         = ESP_OK;
    for (int i = 0; i < 3 && err == ESP_OK; i++) {
        err = rmt_transmit(tx, encoder, symbols.data(), symbols.size() * sizeof(rmt_symbol_word_t), &transmit_config);
        if (err == ESP_OK) {
            err = rmt_tx_wait_all_done(tx, 1000);
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }

    rmt_disable(tx);
    rmt_del_encoder(encoder);
    rmt_del_channel(tx);

    if (err != ESP_OK) {
        error = "IR transmit failed";
        return false;
    }
    ESP_LOGI(TAG, "Sent '%s' (%u durations)", name.c_str(), (unsigned)durations.size());
    return true;
}

std::vector<std::string> ir_remote::list()
{
    auto files = sd_paths::listFiles(kDir, ".txt");
    for (auto& f : files) {
        f = f.substr(0, f.size() - 4);
    }
    return files;
}

/* ------------------------------ Self test (debug) ------------------------------ */

// Transmit a NEC frame while listening with our own receiver, in both output polarities, and log what came back.
// Put a hand or white paper ~10 cm in front of the robot so the IR reflects into the receiver
static int self_test_once(bool invert)
{
    rmt_rx_channel_config_t rx_config = {};
    rx_config.gpio_num                = kRxPin;
    rx_config.clk_src                 = RMT_CLK_SRC_DEFAULT;
    rx_config.resolution_hz           = kResolutionHz;
    rx_config.mem_block_symbols       = kRxSymbols;
    rmt_channel_handle_t rx           = nullptr;
    if (rmt_new_rx_channel(&rx_config, &rx) != ESP_OK) {
        return -1;
    }
    QueueHandle_t queue          = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done             = on_rx_done;
    rmt_rx_register_event_callbacks(rx, &cbs, queue);
    rmt_enable(rx);
    auto* buf = (rmt_symbol_word_t*)heap_caps_malloc(kRxSymbols * sizeof(rmt_symbol_word_t), MALLOC_CAP_INTERNAL);
    rmt_receive_config_t rc = {};
    rc.signal_range_min_ns  = kGlitchNs;
    rc.signal_range_max_ns  = kFrameEndNs;
    rmt_receive(rx, buf, kRxSymbols * sizeof(rmt_symbol_word_t), &rc);

    rmt_tx_channel_config_t tx_config = {};
    tx_config.gpio_num                = kTxPin;
    tx_config.clk_src                 = RMT_CLK_SRC_DEFAULT;
    tx_config.resolution_hz           = kResolutionHz;
    tx_config.mem_block_symbols       = 48;
    tx_config.trans_queue_depth       = 2;
    tx_config.flags.invert_out        = invert;
    rmt_channel_handle_t tx           = nullptr;
    rmt_new_tx_channel(&tx_config, &tx);
    rmt_carrier_config_t carrier = {};
    carrier.frequency_hz         = kCarrierHz;
    carrier.duty_cycle           = 0.33f;
    rmt_apply_carrier(tx, &carrier);
    rmt_encoder_handle_t enc      = nullptr;
    rmt_copy_encoder_config_t ecf = {};
    rmt_new_copy_encoder(&ecf, &enc);
    rmt_enable(tx);

    // NEC: 9 ms mark, 4.5 ms space, 32 bits (0x20DF10EF), stop mark
    std::vector<rmt_symbol_word_t> sym;
    auto add = [&sym](uint32_t mark, uint32_t space) {
        rmt_symbol_word_t s = {};
        s.duration0 = mark;
        s.level0    = 1;
        s.duration1 = space;
        s.level1    = 0;
        sym.push_back(s);
    };
    add(9000, 4500);
    uint32_t code = 0x20DF10EF;
    for (int i = 0; i < 32; i++) {
        add(560, (code >> i) & 1 ? 1690 : 560);
    }
    add(560, 0);
    rmt_transmit_config_t tc = {};
    rmt_transmit(tx, enc, sym.data(), sym.size() * sizeof(rmt_symbol_word_t), &tc);
    rmt_tx_wait_all_done(tx, 1000);

    rmt_rx_done_event_data_t done = {};
    int got = -2;
    if (xQueueReceive(queue, &done, pdMS_TO_TICKS(300)) == pdTRUE) {
        got = done.num_symbols;
        if (got > 0) {
            const auto& s = done.received_symbols[0];
            ESP_LOGI(TAG, "  first symbol: L%d %u / L%d %u", s.level0, s.duration0, s.level1, s.duration1);
        }
    }

    rmt_disable(tx);
    rmt_del_encoder(enc);
    rmt_del_channel(tx);
    rmt_disable(rx);
    rmt_del_channel(rx);
    vQueueDelete(queue);
    heap_caps_free(buf);
    return got;
}

static void self_test_task(void*)
{
    vTaskDelay(pdMS_TO_TICKS(20000));
    for (int round = 0; round < 6; round++) {
        bool invert = round % 2;
        int got;
        {
            std::lock_guard<std::mutex> lock(_ir_mutex);
            got = self_test_once(invert);
        }
        ESP_LOGW(TAG, "IR SELF TEST round %d polarity %s: receiver got %d symbols (-2 = nothing)", round,
                 invert ? "B(inverted)" : "A(normal)", got);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    vTaskDelete(nullptr);
}

void ir_remote::startSelfTest()
{
    xTaskCreate(self_test_task, "ir_selftest", 4096, nullptr, 2, nullptr);
}
