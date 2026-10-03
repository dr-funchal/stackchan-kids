/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_features.h"
#include "sd_card.h"
#include <application.h>
#include <board.h>
#include <audio/demuxer/ogg_demuxer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>

static const char* TAG = "SdFeatures";

// Feature tasks keep their stacks in PSRAM: internal RAM is tight during conversations
static bool start_psram_task(TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio = 2)
{
    TaskHandle_t handle = nullptr;
    BaseType_t ok =
        xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, &handle, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Cannot start task %s", name);
        return false;
    }
    return true;
}

static void delete_self()
{
    vTaskDeleteWithCaps(nullptr);
}

/* ---------------------------------- Paths --------------------------------- */

std::string sd_paths::sanitize(const std::string& name)
{
    // Two-byte UTF-8 Latin-1 letters used in Portuguese, folded to ASCII
    static const struct {
        uint8_t second;
        char ascii;
    } latin1[] = {{0xA0, 'a'}, {0xA1, 'a'}, {0xA2, 'a'}, {0xA3, 'a'}, {0xA4, 'a'}, {0xA7, 'c'}, {0xA8, 'e'},
                  {0xA9, 'e'}, {0xAA, 'e'}, {0xAD, 'i'}, {0xB3, 'o'}, {0xB4, 'o'}, {0xB5, 'o'}, {0xBA, 'u'},
                  {0xBC, 'u'}, {0x80, 'a'}, {0x81, 'a'}, {0x82, 'a'}, {0x83, 'a'}, {0x87, 'c'}, {0x89, 'e'},
                  {0x8A, 'e'}, {0x8D, 'i'}, {0x93, 'o'}, {0x94, 'o'}, {0x95, 'o'}, {0x9A, 'u'}};

    std::string out;
    for (size_t i = 0; i < name.size() && out.size() < 40; i++) {
        uint8_t c = name[i];
        char mapped = '_';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.') {
            mapped = c;
        } else if (c >= 'A' && c <= 'Z') {
            mapped = c - 'A' + 'a';
        } else if (c == 0xC3 && i + 1 < name.size()) {
            uint8_t next = name[++i];
            for (auto& l : latin1) {
                if (l.second == next) {
                    mapped = l.ascii;
                }
            }
        } else if (c >= 0x80) {
            // Skip the rest of any other multi-byte character
            while (i + 1 < name.size() && ((uint8_t)name[i + 1] & 0xC0) == 0x80) {
                i++;
            }
        }
        if (mapped == '_' && (out.empty() || out.back() == '_')) {
            continue;
        }
        out += mapped;
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

static std::vector<std::string> list_dir(const char* dir, bool wantDirs, const char* extension)
{
    std::vector<std::string> names;
    sd_card::BusGuard guard;
    DIR* d = opendir(dir);
    if (!d) {
        return names;
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        bool is_dir = entry->d_type == DT_DIR;
        if (is_dir != wantDirs) {
            continue;
        }
        if (extension) {
            size_t len = strlen(entry->d_name);
            size_t ext = strlen(extension);
            if (len < ext || strcasecmp(entry->d_name + len - ext, extension) != 0) {
                continue;
            }
        }
        names.emplace_back(entry->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> sd_paths::listFiles(const char* dir, const char* extension)
{
    return list_dir(dir, false, extension);
}

std::vector<std::string> sd_paths::listDirs(const char* dir)
{
    return list_dir(dir, true, nullptr);
}

static std::string format_time(const char* fmt)
{
    time_t t = time(nullptr);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    if (tm_info.tm_year + 1900 < 2024) {
        return "";
    }
    char buf[32];
    strftime(buf, sizeof(buf), fmt, &tm_info);
    return buf;
}

std::string sd_paths::today()
{
    return format_time("%Y-%m-%d");
}

std::string sd_paths::now()
{
    return format_time("%Y-%m-%d %H:%M:%S");
}

/* ---------------------------------- Diary --------------------------------- */

static QueueHandle_t _diary_queue = nullptr;

// Lines are batched and written every 30 s (or at 2 KB), and only when internal DMA memory allows an SD
// transfer: writing every sentence during conversations ran the SPI driver out of memory
static constexpr uint32_t kDiaryFlushMs   = 30 * 1000;
static constexpr size_t kDiaryFlushBytes  = 2048;
static constexpr size_t kDiaryMaxPending  = 16 * 1024;  // Drop the oldest text beyond this

static void diary_task(void*)
{
    std::string pending;
    TickType_t last_flush = xTaskGetTickCount();
    for (;;) {
        std::string* line = nullptr;
        if (xQueueReceive(_diary_queue, &line, pdMS_TO_TICKS(5000)) == pdTRUE && line) {
            pending += *line;
            delete line;
            if (pending.size() > kDiaryMaxPending) {
                pending.erase(0, pending.size() - kDiaryMaxPending);
            }
        }
        bool due = pending.size() >= kDiaryFlushBytes ||
                   (!pending.empty() && xTaskGetTickCount() - last_flush >= pdMS_TO_TICKS(kDiaryFlushMs));
        if (!due || !sd_card::hasDmaHeadroom()) {
            continue;
        }
        std::string day  = sd_paths::today();
        std::string path = std::string(sd_paths::kDiary) + "/" + (day.empty() ? "sem-data" : day) + ".txt";
        sd_card::BusGuard guard;
        FILE* f = fopen(path.c_str(), "a");
        if (f) {
            fputs(pending.c_str(), f);
            fclose(f);
            pending.clear();
        } else {
            ESP_LOGW(TAG, "Cannot write diary %s", path.c_str());
        }
        last_flush = xTaskGetTickCount();
    }
}

void sd_diary::log(const char* who, const char* text)
{
    if (!sd_card::isMounted() || !text || !text[0]) {
        return;
    }
    if (!_diary_queue) {
        _diary_queue = xQueueCreate(16, sizeof(std::string*));
        if (!_diary_queue || !start_psram_task(diary_task, "sd_diary", 4096, nullptr, 1)) {
            return;
        }
    }

    std::string time_str = sd_paths::now();
    auto* line = new std::string((time_str.empty() ? std::string("--:--:--") : time_str.substr(11)) + " " + who +
                                 ": " + text + "\n");
    if (xQueueSend(_diary_queue, &line, 0) != pdTRUE) {
        delete line;  // Never block the conversation for the diary
    }
}

/* --------------------------------- Recorder ------------------------------- */

namespace {
enum RecorderState : int { kRecIdle, kRecArmed, kRecRecording, kRecSaving };
std::atomic<int> _rec_state{kRecIdle};
std::string _rec_name;
int _rec_seconds         = 0;
int _rec_rate            = 0;
int16_t* _rec_buffer     = nullptr;
size_t _rec_capacity     = 0;
std::atomic<size_t> _rec_length{0};
// Processed (16 kHz) copy recorded in parallel
constexpr int kProcessedRate = 16000;
int16_t* _proc_buffer        = nullptr;
size_t _proc_capacity        = 0;
std::atomic<size_t> _proc_length{0};
}  // namespace

/* Level stats for diagnosing speech recognition: how loud, how noisy, clipped, and when the voice starts */
struct AudioStats {
    int peak_db       = -99;
    int rms_db        = -99;
    int noise_db      = -99;  // Quietest 100 ms window
    float clipped_pct = 0;
    int voice_ms      = -1;   // First 20 ms window clearly above the noise floor
};

static int to_db(double value)
{
    return value <= 1 ? -99 : (int)lround(20.0 * log10(value / 32768.0));
}

class StatsAccumulator {
public:
    explicit StatsAccumulator(int rate) : _rate(rate) {}
    void add(const int16_t* data, size_t n)
    {
        for (size_t i = 0; i < n; i++) {
            int32_t v = data[i];
            int32_t a = v < 0 ? -v : v;
            _peak     = std::max(_peak, a);
            _sum_sq += (double)v * v;
            _clipped += a >= 32000;
            _count++;
            _win_sq += (double)v * v;
            if (++_win_n == _rate / 50) {  // 20 ms windows
                _windows.push_back(sqrt(_win_sq / _win_n));
                _win_sq = 0;
                _win_n  = 0;
            }
        }
    }
    AudioStats result() const
    {
        AudioStats s;
        if (_count == 0) {
            return s;
        }
        s.peak_db     = to_db(_peak);
        s.rms_db      = to_db(sqrt(_sum_sq / _count));
        s.clipped_pct = 100.0f * _clipped / _count;
        double noise  = 1e9;
        for (size_t i = 0; i + 5 <= _windows.size(); i++) {  // 5 x 20 ms
            double m = 0;
            for (size_t j = 0; j < 5; j++) {
                m += _windows[i + j];
            }
            noise = std::min(noise, m / 5);
        }
        s.noise_db = noise < 1e9 ? to_db(noise) : -99;
        for (size_t i = 0; i < _windows.size(); i++) {
            if (_windows[i] > std::max(noise * 4, 300.0)) {
                s.voice_ms = i * 20;
                break;
            }
        }
        return s;
    }

private:
    int _rate;
    int32_t _peak    = 0;
    double _sum_sq   = 0;
    size_t _clipped  = 0;
    size_t _count    = 0;
    double _win_sq   = 0;
    int _win_n       = 0;
    std::vector<double> _windows;
};

static void log_stats(const char* label, const AudioStats& s)
{
    ESP_LOGI(TAG, "AUDIO %s: peak %d dBFS, rms %d dBFS, noise %d dBFS, SNR ~%d dB, clipped %.1f%%, voice starts at %d ms",
             label, s.peak_db, s.rms_db, s.noise_db, s.rms_db - s.noise_db, s.clipped_pct, s.voice_ms);
}

static void write_le(FILE* f, uint32_t value, int bytes);

static void write_wav(const std::string& path, const int16_t* data, size_t samples, int rate)
{
    uint32_t bytes = samples * sizeof(int16_t);
    FILE* f        = fopen(path.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s", path.c_str());
        return;
    }
    // 16-bit mono PCM WAV
    fwrite("RIFF", 1, 4, f);
    write_le(f, 36 + bytes, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    write_le(f, 16, 4);
    write_le(f, 1, 2);
    write_le(f, 1, 2);
    write_le(f, rate, 4);
    write_le(f, rate * 2, 4);
    write_le(f, 2, 2);
    write_le(f, 16, 2);
    fwrite("data", 1, 4, f);
    write_le(f, bytes, 4);
    fwrite(data, 1, bytes, f);
    fclose(f);
}

static void write_le(FILE* f, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        fputc((value >> (8 * i)) & 0xFF, f);
    }
}

static void recorder_save_task(void*)
{
    std::string dir = std::string(sd_paths::kVoices) + "/" + _rec_name;
    sd_card::BusGuard guard;  // One short WAV write; the screen pauses for it
    mkdir(dir.c_str(), 0775);

    std::string stamp = sd_paths::now();
    std::replace(stamp.begin(), stamp.end(), ' ', '_');
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    std::string base = dir + "/" + (stamp.empty() ? "amostra_" + std::to_string(time(nullptr)) : stamp);

    size_t samples = _rec_length.load();
    write_wav(base + ".wav", _rec_buffer, samples, _rec_rate);
    size_t processed = _proc_buffer ? _proc_length.load() : 0;
    if (processed) {
        write_wav(base + "_processado.wav", _proc_buffer, processed, kProcessedRate);
    }
    ESP_LOGI(TAG, "Voice sample saved: %s.wav (%u samples @ %d Hz)", base.c_str(), (unsigned)samples, _rec_rate);
    sd_diary::log("sistema", ("amostra de voz gravada: " + _rec_name).c_str());

    StatsAccumulator raw(_rec_rate);
    raw.add(_rec_buffer, samples);
    log_stats("microfone", raw.result());
    if (processed) {
        StatsAccumulator proc(kProcessedRate);
        proc.add(_proc_buffer, processed);
        log_stats("processado (enviado ao servidor)", proc.result());
    }
    heap_caps_free(_proc_buffer);
    _proc_buffer = nullptr;

    heap_caps_free(_rec_buffer);
    _rec_buffer = nullptr;
    _rec_state.store(kRecIdle);
    delete_self();
}

bool sd_recorder::arm(const std::string& name, int seconds)
{
    std::string safe = sd_paths::sanitize(name);
    if (!sd_card::isMounted() || safe.empty()) {
        return false;
    }
    int expected = kRecIdle;
    if (!_rec_state.compare_exchange_strong(expected, kRecArmed)) {
        return false;
    }
    _rec_name    = safe;
    _rec_seconds = std::clamp(seconds, 3, 20);
    ESP_LOGI(TAG, "Voice recording armed for '%s', %d s", _rec_name.c_str(), _rec_seconds);
    return true;
}

void sd_recorder::onListening()
{
    if (_rec_state.load() != kRecArmed) {
        return;
    }
    auto codec = Board::GetInstance().GetAudioCodec();
    _rec_rate     = codec ? codec->input_sample_rate() : 16000;
    _rec_capacity = (size_t)_rec_rate * _rec_seconds;
    _rec_buffer   = (int16_t*)heap_caps_malloc(_rec_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!_rec_buffer) {
        ESP_LOGE(TAG, "No memory for voice recording");
        _rec_state.store(kRecIdle);
        return;
    }
    _proc_capacity = (size_t)kProcessedRate * (_rec_seconds + 2);
    _proc_buffer   = (int16_t*)heap_caps_malloc(_proc_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    _proc_length.store(0);
    _rec_length.store(0);
    _rec_state.store(kRecRecording);
    ESP_LOGI(TAG, "Recording voice sample...");
}

void sd_recorder::feed(const int16_t* data, int samples, int channels, int sampleRate)
{
    if (_rec_state.load(std::memory_order_relaxed) != kRecRecording || channels <= 0) {
        return;
    }

    // Keep only the microphone channel (channel 0); the other one is the playback reference
    size_t length = _rec_length.load(std::memory_order_relaxed);
    for (int i = 0; i < samples && length < _rec_capacity; i += channels) {
        _rec_buffer[length++] = data[i];
    }
    _rec_length.store(length, std::memory_order_relaxed);

    if (length >= _rec_capacity) {
        _rec_state.store(kRecSaving);
        if (!start_psram_task(recorder_save_task, "sd_rec_save", 6144, nullptr)) {
            heap_caps_free(_rec_buffer);
            _rec_buffer = nullptr;
            _rec_state.store(kRecIdle);
        }
    }
}

void sd_recorder::feedProcessed(const int16_t* data, size_t samples)
{
    if (_rec_state.load(std::memory_order_relaxed) != kRecRecording || !_proc_buffer) {
        return;
    }
    size_t length = _proc_length.load(std::memory_order_relaxed);
    size_t n      = std::min(samples, _proc_capacity - length);
    memcpy(_proc_buffer + length, data, n * sizeof(int16_t));
    _proc_length.store(length + n, std::memory_order_relaxed);
}

static void analyze_task(void*)
{
    vTaskDelay(pdMS_TO_TICKS(25000));  // After boot and activation settle
    auto* chunk = (int16_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    for (auto& child : sd_paths::listDirs(sd_paths::kVoices)) {
        std::string dir = std::string(sd_paths::kVoices) + "/" + child;
        for (auto& file : sd_paths::listFiles(dir.c_str(), ".wav")) {
            std::string path = dir + "/" + file;
            FILE* f;
            uint8_t header[44];
            {
                sd_card::BusGuard guard;
                f = fopen(path.c_str(), "rb");
                if (f && fread(header, 1, 44, f) != 44) {
                    fclose(f);
                    f = nullptr;
                }
            }
            if (!f || !chunk) {
                continue;
            }
            int rate = header[24] | header[25] << 8 | header[26] << 16 | header[27] << 24;
            StatsAccumulator acc(rate > 0 ? rate : 16000);
            size_t n;
            do {
                {
                    sd_card::BusGuard guard;
                    n = fread(chunk, sizeof(int16_t), 4096, f);
                }
                acc.add(chunk, n);
                vTaskDelay(1);
            } while (n > 0);
            {
                sd_card::BusGuard guard;
                fclose(f);
            }
            log_stats((child + "/" + file).c_str(), acc.result());
        }
    }
    heap_caps_free(chunk);
    delete_self();
}

void sd_recorder::analyzeExisting()
{
    if (sd_card::isMounted()) {
        start_psram_task(analyze_task, "sd_analyze", 6144, nullptr, 1);
    }
}

/* ---------------------------------- Story --------------------------------- */

namespace {
std::mutex _story_mutex;
std::string _story_pending;
std::atomic<bool> _story_playing{false};
std::atomic<bool> _story_stop{false};
std::atomic<bool> _story_closing{false};
std::function<void(bool)> _story_hook;
}  // namespace

std::vector<std::string> sd_story::list()
{
    return sd_paths::listFiles(sd_paths::kStories, ".ogg");
}

bool sd_story::request(const std::string& name)
{
    if (!sd_card::isMounted()) {
        return false;
    }

    // Accept the exact file name, or a loose match ("lobo" -> "historia_do_lobo.ogg")
    std::string wanted = sd_paths::sanitize(name);
    std::string found;
    for (auto& file : list()) {
        std::string base = sd_paths::sanitize(file.substr(0, file.size() - 4));
        if (file == name || base == wanted) {
            found = file;
            break;
        }
        if (found.empty() && !wanted.empty() && base.find(wanted) != std::string::npos) {
            found = file;
        }
    }
    if (found.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(_story_mutex);
    _story_pending = std::string(sd_paths::kStories) + "/" + found;
    ESP_LOGI(TAG, "Story queued: %s", _story_pending.c_str());
    return true;
}

void sd_story::stop()
{
    {
        std::lock_guard<std::mutex> lock(_story_mutex);
        _story_pending.clear();
    }
    if (_story_playing.load()) {
        _story_stop.store(true);
    }
}

bool sd_story::isPlaying()
{
    return _story_playing.load();
}

void sd_story::setPlaybackHook(std::function<void(bool playing)> hook)
{
    _story_hook = std::move(hook);
}

static void story_task(void* arg)
{
    std::unique_ptr<std::string> path(static_cast<std::string*>(arg));
    auto& audio = Application::GetInstance().GetAudioService();
    auto codec  = Board::GetInstance().GetAudioCodec();

    FILE* f;
    {
        sd_card::BusGuard guard;
        f = fopen(path->c_str(), "rb");
    }
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path->c_str());
        _story_playing.store(false);
        delete_self();
        return;
    }

    ESP_LOGI(TAG, "Playing story %s", path->c_str());
    sd_diary::log("sistema", ("historia: " + path->substr(strlen(sd_paths::kStories) + 1)).c_str());
    if (_story_hook) {
        _story_hook(true);
    }
    if (codec && !codec->output_enabled()) {
        codec->EnableOutput(true);
    }

    // Stream: read a chunk, demux Ogg pages into Opus packets, feed the decoder (blocks while its queue is full)
    auto demuxer = std::make_unique<OggDemuxer>();
    demuxer->OnDemuxerFinished([&audio](const uint8_t* data, int sample_rate, size_t size) {
        if (_story_stop.load()) {
            return;
        }
        auto packet            = std::make_unique<AudioStreamPacket>();
        packet->sample_rate    = sample_rate;
        packet->frame_duration = 60;
        packet->payload.assign(data, data + size);
        audio.PushPacketToDecodeQueue(std::move(packet), true);
    });
    demuxer->Reset();

    auto* chunk = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    size_t read = 0;
    while (chunk && !_story_stop.load()) {
        while (!sd_card::hasDmaHeadroom() && !_story_stop.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));  // Wait for internal DMA memory rather than crash the SPI driver
        }
        {
            sd_card::BusGuard guard;
            read = fread(chunk, 1, 4096, f);
        }
        if (read == 0) {
            break;
        }
        demuxer->Process(chunk, read);  // Outside the guard: it blocks on the decoder queue
    }
    heap_caps_free(chunk);
    {
        sd_card::BusGuard guard;
        fclose(f);
    }

    if (_story_stop.load()) {
        audio.ResetDecoder();
        ESP_LOGI(TAG, "Story stopped");
    } else {
        // Let the tail of the story play out
        while (!audio.IsIdle() && !_story_stop.load()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "Story finished");
    }

    if (_story_hook) {
        _story_hook(false);
    }
    _story_stop.store(false);
    _story_playing.store(false);
    delete_self();
}

void sd_story::onDeviceStatus(bool idle, bool listening)
{
    // Someone started talking to the robot: the story yields
    if (_story_playing.load() && !idle) {
        _story_stop.store(true);
    }

    std::string pending;
    {
        std::lock_guard<std::mutex> lock(_story_mutex);
        pending = _story_pending;
    }
    if (pending.empty()) {
        _story_closing.store(false);
        return;
    }

    // A story was asked for: end the conversation once the robot has answered, then play
    if (listening && !_story_closing.exchange(true)) {
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateListening) {
                app.ToggleChatState();
            }
        });
    }

    if (idle && !_story_playing.load()) {
        {
            std::lock_guard<std::mutex> lock(_story_mutex);
            _story_pending.clear();
        }
        _story_closing.store(false);
        _story_stop.store(false);
        _story_playing.store(true);
        if (!start_psram_task(story_task, "sd_story", 8192, new std::string(pending), 4)) {
            _story_playing.store(false);
        }
    }
}
