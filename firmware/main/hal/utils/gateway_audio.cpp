/*
 * SPDX-License-Identifier: MIT
 */
#include "gateway_audio.h"
#include "sd_card.h"
#include "sd_features.h"
#include <application.h>
#include <board.h>
#include <mcp_server.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>

static const char* TAG = "GatewayAudio";

static constexpr const char* kAudioBase = "https://m5.pulpfy.com/a/";
static constexpr size_t kMaxBytes       = 20 * 1024 * 1024;  // ~110 min at the gateway's 24 kbps
static constexpr size_t kChunk          = 4096;
static constexpr int kMaxDmaWaits       = 200;  // x 50 ms: give up rather than wait forever for DMA memory

namespace {
std::atomic<bool> _busy{false};

struct Job {
    std::string code;
    std::string base;  // file name without extension, in historias/ so the story player finds it
};
}  // namespace

static bool valid_code(const std::string& code)
{
    if (code.size() < 16 || code.size() > 64) {
        return false;
    }
    for (char c : code) {
        if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

static std::string story_path(const std::string& base)
{
    return std::string(sd_paths::kStories) + "/" + base + ".ogg";
}

static bool file_exists(const std::string& path)
{
    sd_card::BusGuard guard;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

// Queue the file in the story player. It starts when the conversation ends; if the robot is already idle no status
// change will come, so nudge the player from the main task.
static void play(const std::string& base)
{
    sd_story::request(base + ".ogg");
    Application::GetInstance().Schedule([]() {
        auto state = Application::GetInstance().GetDeviceState();
        sd_story::onDeviceStatus(state == kDeviceStateIdle, state == kDeviceStateListening);
    });
}

static bool download(const std::string& url, const std::string& path)
{
    const std::string part = path + ".part";
    bool ok   = false;
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
    if (!http->Open("GET", url)) {
        ESP_LOGW(TAG, "Cannot reach the gateway");
        return false;
    }
    int status    = http->GetStatusCode();
    size_t length = http->GetBodyLength();
    if (status != 200 || length == 0 || length > kMaxBytes) {
        ESP_LOGW(TAG, "Download refused: HTTP %d, %u bytes", status, (unsigned)length);
        http->Close();
        return false;
    }

    FILE* f;
    {
        sd_card::BusGuard guard;
        f = fopen(part.c_str(), "wb");
    }
    auto* buf    = (char*)heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM);
    size_t total = 0;
    while (f && buf && total < length) {
        int n = http->Read(buf, kChunk);
        if (n <= 0) {
            break;
        }
        int waits = 0;
        while (!sd_card::hasDmaHeadroom() && waits++ < kMaxDmaWaits) {
            vTaskDelay(pdMS_TO_TICKS(50));  // FATFS needs internal DMA memory for every SD transfer
        }
        if (waits > kMaxDmaWaits) {
            break;
        }
        size_t written;
        {
            sd_card::BusGuard guard;
            written = fwrite(buf, 1, n, f);
        }
        if (written != (size_t)n) {
            break;
        }
        total += n;
    }
    heap_caps_free(buf);
    http->Close();
    if (f) {
        sd_card::BusGuard guard;
        fclose(f);
        if (total == length) {
            remove(path.c_str());
            ok = rename(part.c_str(), path.c_str()) == 0;
        }
        if (!ok) {
            remove(part.c_str());
        }
    }
    ESP_LOGI(TAG, "Downloaded %u of %u bytes to %s: %s", (unsigned)total, (unsigned)length, path.c_str(),
             ok ? "ok" : "failed");
    return ok;
}

static void download_task(void* arg)
{
    std::unique_ptr<Job> job(static_cast<Job*>(arg));
    if (download(std::string(kAudioBase) + job->code, story_path(job->base))) {
        sd_diary::log("sistema", ("musica do gateway: " + job->base).c_str());
        play(job->base);
    }
    job.reset();
    _busy.store(false);
    vTaskDeleteWithCaps(nullptr);
}

void gateway_audio::registerMcpTools()
{
    McpServer::GetInstance().AddTool(
        "self.gateway.play_audio",
        "Plays a song from the Stack-Chan gateway on the robot's own speaker. Call it ONLY when the gateway music tool "
        "tells you to, with exactly the code and name it gives. The song starts when you finish talking; "
        "talking to the robot stops it.",
        PropertyList({Property("code", kPropertyTypeString), Property("name", kPropertyTypeString)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto code = properties["code"].value<std::string>();
            auto name = properties["name"].value<std::string>();
            if (!sd_card::isMounted()) {
                return std::string("The robot has no SD card, so it cannot play songs. Tell the child kindly.");
            }
            if (!valid_code(code) || name.empty()) {
                return std::string("Invalid code. Ask the gateway music tool again for a new one.");
            }
            std::string base = "musica_" + sd_paths::sanitize(name.substr(0, 48));
            if (file_exists(story_path(base))) {
                play(base);  // already on the card from an earlier request
                return std::string("OK: the song starts as soon as you finish talking. Say so in one short sentence.");
            }
            if (_busy.exchange(true)) {
                return std::string("Another song is still downloading. Ask the child to wait a moment.");
            }
            auto* job = new Job{code, base};
            // Network and SD work in its own task: MCP tools run on the main task and must not block
            if (xTaskCreatePinnedToCoreWithCaps(download_task, "gw_audio", 8192, job, 3, nullptr, tskNO_AFFINITY,
                                                MALLOC_CAP_SPIRAM) != pdPASS) {
                delete job;
                _busy.store(false);
                return std::string("Could not start the download.");
            }
            return std::string(
                "OK: the song is downloading and starts right after you finish talking. Say so in one short sentence.");
        });
    ESP_LOGI(TAG, "Gateway audio tool registered");
}
