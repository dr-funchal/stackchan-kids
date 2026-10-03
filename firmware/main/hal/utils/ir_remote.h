/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>
#include <vector>

/**
 * @brief Learn and replay infrared remote buttons (TV, A/C, fan...) with the body's IR receiver (GPIO10)
 * and IR LED (GPIO5).
 *
 * Buttons are stored as raw mark/space timings, so any protocol works. Files live on the SD card in
 * controles/<name>.txt: first line is the carrier in Hz, second line the comma-separated durations in us
 * (mark, space, mark, ...)
 */
namespace ir_remote {

inline constexpr const char* kDir = "/sdcard/stackchan/controles";

// Wait up to timeoutMs for a remote button and save it under `name`. Blocks the caller
bool learn(const std::string& name, int timeoutMs, std::string& error);

bool send(const std::string& name, std::string& error);

std::vector<std::string> list();

// Debug: loopback test of the IR LED through our own receiver, logged ~20 s after boot
void startSelfTest();

}  // namespace ir_remote
