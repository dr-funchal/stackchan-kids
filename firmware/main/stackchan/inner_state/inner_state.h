/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "inner_state_core.h"
#include <string>

/**
 * @brief The robot's drives (energy, longing, curiosity, mood) running on the device, see inner_state_core.h.
 * They shape the idle motion and expression, and the AI hears about them, with the body state, through
 * self.memory recall "eu"
 */
namespace inner_state {

// LVGL task (lock held), every 250 ms: advances the drives and plays a pending reunion greeting
void tick(bool napping, bool talking);

// From any task
void onEvent(Event e);

Behavior behavior();

// Body and feelings right now, in plain words for the AI (battery, posture, time alone, drives)
std::string bodyReport();

}  // namespace inner_state
