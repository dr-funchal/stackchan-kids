/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief Papa-Letras: a word game for 4-6 year olds, played in turns.
 *
 * The robot picks a theme (animals, fruits...) and walks the alphabet; on each turn a child says a word of
 * the theme starting with the current letter, and the panda "eats" the letter. The AI judges the words and
 * leads the conversation; this module keeps the rules (letter order, turns, no repeated words, stars) and
 * shows the big letter on screen, through the self.game.* MCP tools
 */
namespace papa_letras {

void registerMcpTools();

bool isActive();

// From the display: the robot started (true) or stopped (false) listening. Drives the 20 s turn clock on the LEDs
void onListening(bool listening);

// Head controls during a game: quick tap asks for a hint, petting (swipe) skips the letter
void onHeadTap();
void onHeadPet();

}  // namespace papa_letras
