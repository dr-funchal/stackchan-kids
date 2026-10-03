/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Features that live on the microSD card (/sdcard/stackchan): conversation diary, voice samples,
 * stories, a web page to manage the files, and the AI tools that drive them
 */

namespace sd_paths {
inline constexpr const char* kRoot    = "/sdcard/stackchan";
inline constexpr const char* kDiary   = "/sdcard/stackchan/diario";
inline constexpr const char* kVoices  = "/sdcard/stackchan/vozes";
inline constexpr const char* kStories = "/sdcard/stackchan/historias";
inline constexpr const char* kSkins   = "/sdcard/stackchan/skins";

// Lowercase, accents/spaces/punctuation folded to '_', so names are safe as file names
std::string sanitize(const std::string& name);
// Files (not folders) in a directory, sorted
std::vector<std::string> listFiles(const char* dir, const char* extension = nullptr);
std::vector<std::string> listDirs(const char* dir);
// "2026-10-01" / "2026-10-01 18:55:02"; empty if the clock was never set
std::string today();
std::string now();
}  // namespace sd_paths

/**
 * @brief Conversation diary: one text file per day in diario/
 */
namespace sd_diary {
void log(const char* who, const char* text);
}

/**
 * @brief Voice samples for speaker enrollment, saved as WAV in vozes/<name>/
 */
namespace sd_recorder {
// Record `seconds` of the microphone starting the next time the robot listens
bool arm(const std::string& name, int seconds);
// From the display, when the device enters the listening state
void onListening();
// From the audio codec input path (audio task): raw microphone
void feed(const int16_t* data, int samples, int channels, int sampleRate);
// From the audio processor output (16 kHz mono after AEC/AGC): exactly what is sent to the server
void feedProcessed(const int16_t* data, size_t samples);
// Log level/noise stats of every WAV already in vozes/ (runs in the background)
void analyzeExisting();
}  // namespace sd_recorder

/**
 * @brief Story player: streams Ogg Opus (mono 16 kHz) files from historias/
 */
namespace sd_story {
std::vector<std::string> list();
// Queue a story; it starts once the conversation is over
bool request(const std::string& name);
void stop();
bool isPlaying();
// From the display on every status change
void onDeviceStatus(bool idle, bool listening);
// Display hook, called from the player task when playback starts/stops
void setPlaybackHook(std::function<void(bool playing)> hook);
}  // namespace sd_story

/**
 * @brief Web page on the robot (port 80) to browse, play, upload and delete files on the card
 */
namespace sd_web {
void start();  // Idempotent; call once the network is up
}

/**
 * @brief Register the AI (MCP) tools for voices, stories and skins
 */
void sd_features_register_mcp_tools();
