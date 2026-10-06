/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief Songs from the Stack-Chan gateway (gateway/ in this repo, https://m5.pulpfy.com).
 *
 * The gateway's play_music tool hands the AI a one-time code; the AI calls self.gateway.play_audio with it. The robot
 * downloads the file (already converted by the gateway into the only Ogg Opus shape the decoder survives) to the SD
 * card, cached by name, and the story player plays it once the conversation is over. Only the gateway host is ever
 * contacted: the AI passes a code, never a URL, and no credential lives in the firmware.
 */
namespace gateway_audio {
void registerMcpTools();
}
