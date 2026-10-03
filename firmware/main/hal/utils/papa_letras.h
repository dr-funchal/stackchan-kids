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

}  // namespace papa_letras
