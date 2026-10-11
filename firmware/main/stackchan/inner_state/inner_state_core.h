/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

/**
 * @brief The robot's inner drives (homeostasis): a few numbers that drift with time and with what happens to it,
 * and that shape its idle behavior and what it can tell about how it feels. No ESP dependencies, so it is tested on
 * the Mac (tests/inner_state_test.cpp). All values are 0..100
 *
 * - energy: drains while awake (faster while talking or with a low battery), recharges while napping
 * - longing (saudade): grows while nobody is around, drops with petting and conversations
 * - curiosity: grows when nothing new happens, drops when something does (a conversation, a game, a story)
 * - mood: drifts back to neutral; petting, play and company lift it, shaking and long loneliness lower it
 */
namespace inner_state {

enum class Event {
    HeadPet,            // Petting (swipe) on the head
    Touch,              // Tap on the screen or the head
    ConversationStart,  // Wake word or touch started a chat
    Play,               // A game or a story started
    Shake,              // Someone shook the robot
};

enum class Face { Neutral, Happy, Sad, Sleepy };

struct Behavior {
    Face face            = Face::Neutral;
    float interval_scale = 1.0f;   // Idle motion: >1 = moves less often
    float speed_scale    = 1.0f;   // Idle motion speed
    bool searching       = false;  // Looks around widely, as if searching for someone
    bool drowsy          = false;  // Head low, slow moves
};

struct Drives {
    float energy    = 70;
    float longing   = 30;
    float curiosity = 40;
    float mood      = 60;
};

struct TickInput {
    uint32_t dt_ms     = 0;
    bool napping       = false;
    bool talking       = false;  // In a conversation (listening or speaking)
    uint8_t battery    = 100;
    bool charging      = false;
};

// Thresholds shared by the behavior and the words
static constexpr float kDrowsyBelow  = 25;
static constexpr float kSearchAbove  = 65;
static constexpr float kSadAbove     = 85;
static constexpr float kHappyAbove   = 72;
static constexpr float kReunionAbove = 60;  // Longing that makes a returning person a big moment

class Model {
public:
    const Drives& drives() const
    {
        return _d;
    }

    void set(const Drives& d)
    {
        _d = d;
        clamp_all();
    }

    void tick(const TickInput& in)
    {
        float h = in.dt_ms / 3600000.0f;  // Rates below are per hour
        if (in.napping) {
            _d.energy += 45 * h;
            _d.longing += 15 * h;
            _d.curiosity += 4 * h;
        } else if (in.talking) {
            _d.energy -= 18 * h;
            _d.longing -= 80 * h;
            _d.curiosity -= 25 * h;
            _d.mood += 6 * h;
        } else {
            _d.energy -= 8 * h;
            _d.longing += 35 * h;
            _d.curiosity += 15 * h;
        }
        if (in.battery < 20 && !in.charging) {
            _d.energy -= 30 * h;
        }
        if (in.charging) {
            _d.energy += 10 * h;
        }
        // Mood drifts back to neutral; loneliness weighs on it
        _d.mood += (55 - _d.mood) * std::min(1.0f, 0.4f * h);
        if (_d.longing > kSadAbove) {
            _d.mood -= 10 * h;
        }
        clamp_all();
    }

    /**
     * @brief Applies an event. Returns true for a reunion: someone came back after the robot missed company for a
     * long time (the caller shows a big happy greeting)
     */
    bool event(Event e)
    {
        bool reunion = false;
        switch (e) {
            case Event::HeadPet:
                reunion = _d.longing >= kReunionAbove;
                _d.longing -= 12;
                _d.mood += 8;
                break;
            case Event::Touch:
                reunion = _d.longing >= kReunionAbove;
                _d.longing -= 4;
                _d.mood += 2;
                break;
            case Event::ConversationStart:
                reunion = _d.longing >= kReunionAbove;
                _d.longing -= 15;
                _d.curiosity -= 10;
                _d.mood += 5;
                break;
            case Event::Play:
                _d.curiosity -= 20;
                _d.mood += 10;
                _d.energy -= 3;
                break;
            case Event::Shake:
                _d.mood -= 10;
                _d.curiosity += 5;
                break;
        }
        clamp_all();
        return reunion;
    }

    Behavior behavior() const
    {
        Behavior b;
        if (_d.energy < kDrowsyBelow) {
            b.face           = Face::Sleepy;
            b.drowsy         = true;
            b.interval_scale = 2.0f;
            b.speed_scale    = 0.5f;
            return b;
        }
        if (_d.longing > kSearchAbove) {
            b.searching      = true;
            b.interval_scale = 0.7f;
            b.face           = _d.longing > kSadAbove ? Face::Sad : Face::Neutral;
            return b;
        }
        if (_d.mood > kHappyAbove) {
            b.face = Face::Happy;
        }
        if (_d.curiosity > 70) {
            b.interval_scale = 0.8f;
            b.speed_scale    = 1.2f;
        }
        if (_d.energy < 40) {
            b.interval_scale *= 1.4f;
            b.speed_scale *= 0.8f;
        }
        return b;
    }

    // How it feels, in plain words for the AI
    std::string describe() const
    {
        return "energy " + level(_d.energy) + (_d.energy < kDrowsyBelow ? " (sleepy)" : "") + ", missing company " +
               level(_d.longing) + ", curiosity " + level(_d.curiosity) + ", mood " +
               (_d.mood > kHappyAbove ? "happy" : _d.mood < 35 ? "a bit down" : "calm");
    }

private:
    static std::string level(float v)
    {
        return v >= 75 ? "very high" : v >= 55 ? "high" : v >= 30 ? "medium" : v >= 12 ? "low" : "very low";
    }

    void clamp_all()
    {
        for (float* v : {&_d.energy, &_d.longing, &_d.curiosity, &_d.mood}) {
            *v = std::max(0.0f, std::min(100.0f, *v));
        }
    }

    Drives _d;
};

}  // namespace inner_state
