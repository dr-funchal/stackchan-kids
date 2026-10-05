/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief "O Panda Mandou" + color hunt, for 4-6 year olds, played in turns.
 *
 * The AI gives a command ("o panda mandou tocar na minha cabeca!", "o panda mandou trazer algo vermelho!"),
 * sometimes without "o panda mandou" as a trick. This module arms a detector (head touch, petting, shake,
 * screen touch, or the camera checking a color on the chip), runs the LED clock, scores the turn, and tells
 * the AI what happened by sending a short text as if spoken
 */
namespace panda_mandou {

void registerMcpTools();

bool isActive();

// From the display: a screen tap. Returns true if the game consumed it
bool onScreenTap();

}  // namespace panda_mandou
