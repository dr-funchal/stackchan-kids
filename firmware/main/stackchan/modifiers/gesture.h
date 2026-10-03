/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/hal.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace stackchan {

/**
 * @brief Body language: a short head choreography plus a body LED color for an emotion.
 *
 * Moves only the servos and LEDs 1-11 (LED 0 is the status light), so the face keeps showing the emotion and
 * lip sync. Steps are relative to the pose the head had when the gesture started, and it returns there at the end
 */
class GestureModifier : public Modifier {
public:
    enum class Kind { Happy, Sad, Angry, Surprised, Thinking, Love, Cool, Nod, Listen };

    explicit GestureModifier(Kind kind) : _steps(sequence(kind))
    {
        _active++;
    }

    ~GestureModifier()
    {
        set_leds(0);
        _active--;
    }

    // Modifier ids are recycled, so callers check this instead of holding on to an id
    static bool isActive()
    {
        return _active > 0;
    }

    void _update(Modifiable& stackchan) override
    {
        uint32_t now = GetHAL().millis();
        if (now < _next_step_tick) {
            return;
        }

        auto& motion = stackchan.motion();
        if (!_started) {
            _base    = motion.getCurrentAngles();
            _started = true;
        }

        if (_index >= _steps.size()) {
            // Back to where we were, lights off
            motion.moveWithSpeed(_base.x, _base.y, 300);
            set_leds(0);
            requestDestroy();
            return;
        }

        const auto& step = _steps[_index++];
        int yaw          = std::clamp(_base.x + step.yaw, -1280, 1280);
        int pitch        = std::clamp(_base.y + step.pitch, 0, 900);
        motion.moveWithSpeed(yaw, pitch, step.speed);
        if (step.rgb != kKeep) {
            set_leds(step.rgb);
        }
        _next_step_tick = now + step.holdMs;
    }

private:
    static constexpr uint32_t kKeep = 0xFFFFFFFF;

    struct Step {
        int yaw;    // Tenths of a degree, relative
        int pitch;  // Tenths of a degree, relative (0 is the lowest the head goes)
        int speed;  // 0-1000
        uint32_t holdMs;
        uint32_t rgb;
    };

    static std::vector<Step> sequence(Kind kind)
    {
        switch (kind) {
            case Kind::Happy:  // Bouncy hops, then a big wiggle, warm yellow
                return {{0, 260, 700, 280, 0x302000},  {0, 0, 700, 280, kKeep},    {0, 260, 700, 280, kKeep},
                        {0, 0, 700, 280, kKeep},       {0, 260, 700, 280, kKeep},  {0, 0, 700, 280, kKeep},
                        {-200, 100, 450, 420, kKeep},  {200, 100, 450, 420, kKeep}, {-200, 100, 450, 420, kKeep},
                        {200, 100, 450, 420, kKeep}};
            case Kind::Sad:  // Slow, deep droop, look away, dim blue
                return {{0, -400, 100, 1800, 0x000818}, {-150, -400, 80, 2000, kKeep}, {-60, -350, 80, 1500, kKeep}};
            case Kind::Angry:  // Strong head shakes, red
                return {{-150, 80, 850, 220, 0x300000}, {150, 80, 850, 220, kKeep}, {-150, 80, 850, 220, kKeep},
                        {150, 80, 850, 220, kKeep},     {-150, 80, 850, 220, kKeep}, {150, 80, 850, 220, kKeep},
                        {0, 0, 300, 600, kKeep}};
            case Kind::Surprised:  // Big jerk back, white flash, then a slow "wow" look around
                return {{0, 450, 950, 1100, 0x282828}, {-150, 380, 250, 800, 0x101010}, {150, 380, 250, 800, kKeep},
                        {0, 300, 250, 600, kKeep}};
            case Kind::Thinking:  // Look up and aside, ponder, switch sides, slow purple
                return {{300, 350, 130, 1800, 0x140020}, {320, 380, 60, 1200, kKeep}, {-250, 330, 130, 1600, kKeep}};
            case Kind::Love:  // Long gentle sway, pink
                return {{-120, 150, 200, 750, 0x300818}, {120, 150, 200, 750, kKeep}, {-120, 150, 200, 750, kKeep},
                        {120, 150, 200, 750, kKeep},     {0, 100, 200, 600, kKeep}};
            case Kind::Cool:  // Slow confident nods, cyan
                return {{0, -180, 250, 650, 0x001820}, {0, 120, 250, 650, kKeep}, {0, -180, 250, 650, kKeep},
                        {0, 80, 250, 650, kKeep}};
            case Kind::Nod:  // Two clear nods
                return {{0, -200, 500, 380, kKeep}, {0, 50, 500, 380, kKeep}, {0, -200, 500, 380, kKeep},
                        {0, 0, 500, 380, kKeep}};
            case Kind::Listen:  // Lean in toward whoever is talking
            default:
                return {{0, 220, 250, 3500, kKeep}};
        }
    }

    static void set_leds(uint32_t rgb)
    {
        for (uint8_t i = 1; i < 12; i++) {
            GetHAL().setRgbColor(i, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
        GetHAL().refreshRgb();
    }

    inline static int _active = 0;
    std::vector<Step> _steps;
    size_t _index            = 0;
    uint32_t _next_step_tick = 0;
    bool _started            = false;
    uitk::Vector2i _base;
};

}  // namespace stackchan
