/*
 * SPDX-License-Identifier: MIT
 */
#include "inner_state.h"
#include "../stackchan.h"
#include <hal/hal.h>
#include <esp_log.h>
#include <atomic>
#include <ctime>
#include <mutex>

static const char* TAG = "InnerState";

namespace {
inner_state::Model _model;
std::mutex _mutex;  // Events come from the touch, IMU and main tasks; ticks from the LVGL task

uint32_t _last_tick_ms           = 0;
std::atomic<uint32_t> _last_touch_ms{0};
std::atomic<uint32_t> _last_company_ms{0};   // Last touch or end of a conversation
std::atomic<uint32_t> _alone_before_ms{0};   // How long it was alone before the current conversation
std::atomic<bool> _talking{false};
std::atomic<bool> _pending_reunion{false};
std::atomic<uint8_t> _battery{100};
std::atomic<bool> _charging{false};
}  // namespace

static std::string duration_text(uint32_t ms)
{
    uint32_t minutes = ms / 60000;
    if (minutes < 1) {
        return "less than a minute";
    }
    if (minutes < 60) {
        return std::to_string(minutes) + " min";
    }
    return std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
}

void inner_state::tick(bool napping, bool talking)
{
    uint32_t now = GetHAL().millis();
    if (_last_tick_ms == 0) {
        _last_tick_ms = now;
        _last_company_ms.store(now);
    }
    uint32_t dt   = now - _last_tick_ms;
    _last_tick_ms = now;

    // The battery is read over I2C: every 30 s is plenty
    static uint32_t last_battery_ms = 0;
    if (last_battery_ms == 0 || now - last_battery_ms > 30000) {
        last_battery_ms = now;
        _battery.store(GetHAL().getBatteryLevel());
        _charging.store(GetHAL().isBatteryCharging());
    }

    bool was_talking = _talking.exchange(talking);
    if (was_talking && !talking) {
        _last_company_ms.store(now);  // A conversation just ended
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        TickInput in;
        in.dt_ms    = dt;
        in.napping  = napping;
        in.talking  = talking;
        in.battery  = _battery.load();
        in.charging = _charging.load();
        _model.tick(in);
    }

    // Someone came back after a long time alone: a big, loving greeting (only outside conversations: there the
    // Listen gesture owns the head)
    if (!talking && !napping && _pending_reunion.exchange(false)) {  // A pet that wakes it plays after waking
        ESP_LOGI(TAG, "Reunion greeting");
        GetStackChan().addModifier(std::make_unique<stackchan::GestureModifier>(stackchan::GestureModifier::Kind::Love));
    }

    static uint32_t last_log_ms = 0;
    if (now - last_log_ms > 10 * 60 * 1000) {
        last_log_ms = now;
        std::lock_guard<std::mutex> lock(_mutex);
        auto& d = _model.drives();
        ESP_LOGI(TAG, "energy %.0f, longing %.0f, curiosity %.0f, mood %.0f", d.energy, d.longing, d.curiosity, d.mood);
    }
}

void inner_state::onEvent(Event e)
{
    uint32_t now = GetHAL().millis();
    if (e == Event::ConversationStart) {
        uint32_t since = std::max(_last_touch_ms.load(), _last_company_ms.load());
        _alone_before_ms.store(since ? now - since : 0);
    }
    if (e == Event::HeadPet || e == Event::Touch) {
        _last_touch_ms.store(now);
        _last_company_ms.store(now);
    }
    bool reunion;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        reunion = _model.event(e);
    }
    if (reunion && e == Event::HeadPet) {
        _pending_reunion.store(true);
    }
}

inner_state::Behavior inner_state::behavior()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _model.behavior();
}

std::string inner_state::bodyReport()
{
    uint32_t now = GetHAL().millis();
    // Battery as read by the last tick (I2C stays with the LVGL task)
    std::string text = "YOUR BODY NOW: battery " + std::to_string(_battery.load()) + "%" +
                       (_charging.load() ? " (charging)" : "");
    int tilt = GetHAL().getTiltFromBootDegrees();
    if (tilt >= 0) {
        text += tilt < 20 ? ", standing" : tilt < 60 ? ", tilted (" + std::to_string(tilt) + " degrees)" : ", lying down or fallen over";
    }
    text += ", switched on for " + duration_text(now);
    uint32_t touch = _last_touch_ms.load();
    text += touch ? ", last touched " + duration_text(now - touch) + " ago" : ", not touched since you woke up";
    if (_talking.load()) {
        text += ", alone for " + duration_text(_alone_before_ms.load()) + " before this conversation";
    }
    time_t t = time(nullptr);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    if (tm_info.tm_year + 1900 >= 2024) {
        int h = tm_info.tm_hour;
        text += std::string(", it is ") + (h < 6 ? "night" : h < 12 ? "morning" : h < 18 ? "afternoon" : "evening");
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return text + ". HOW YOU FEEL: " + _model.describe() +
           ". Let it color how you talk (sleepy, glad to have company, curious), never as a complaint.";
}
