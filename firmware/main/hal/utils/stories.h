/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>

/**
 * @brief Stories read aloud by the AI (no recordings): 5 built-in ones plus any .txt in historias/ on the SD card.
 *
 * The AI tends to summarize long texts, so the story is handed over one page (~120 words) at a time with the
 * instruction to read it word for word and ask for the next page (self.historia.* MCP tools)
 */
namespace stories {

void registerMcpTools();

// Copy the stories packed in the firmware (assets_src/historias/*.txt) to the SD card if missing. Boot only
void exportSeeds();

// "Title; Title; ..." of the stories that can be read aloud
std::string listTitles();

// Start reading a story whose title matches `name` (or a random one if allowRandom). On success `result` holds
// the reading rules and page 1 for the AI
bool start(const std::string& name, std::string& result, bool allowRandom);

}  // namespace stories
