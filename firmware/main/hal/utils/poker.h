/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief Poker do Panda: five-card draw, one child (or a team) against the robot, with chips.
 *
 * The child's five cards are drawn at the bottom of the screen and the robot's face down at the top. Tapping a
 * card marks it to be swapped. The AI talks and calls the self.poker MCP tool; this module deals, keeps the
 * chips, plays the robot's side (draw and bets) and judges the showdown (rules in poker_rules.h)
 */
namespace poker {

void registerMcpTools();

bool isActive();

// From the display: the conversation ended (back to standby). Clears the cards off the screen
void onConversationEnded();

}  // namespace poker
