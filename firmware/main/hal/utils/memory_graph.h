/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief Long-term memory on the SD card, as a graph of facts "subject | relation | object"
 * (memoria/grafo.tsv, one fact per line; the parents can read and fix it through the card's web page).
 *
 * The AI reads and writes it through the self.memory MCP tool. The graph lives in PSRAM; the card is only
 * touched by a background task (loading at boot, appending new facts, rewriting after a forget)
 */
namespace memory_graph {

// Needs the card mounted. Starts loading the file in the background
void registerMcpTool();

// The file changed outside the robot (web page upload or delete): load it again
void reload();

}  // namespace memory_graph
