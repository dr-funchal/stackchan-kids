/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_features.h"
#include "sd_card.h"
#include "stories.h"
#include "memory_graph.h"
#include <stackchan/inner_state/inner_state.h>
#include <stackchan/avatar/skins/sd/sd_skin.h>
#include <mcp_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "SdMcp";

static std::string json_list(const std::vector<std::string>& names)
{
    std::string json = "[";
    for (auto& name : names) {
        json += (json.size() > 1 ? ",\"" : "\"") + name + "\"";
    }
    return json + "]";
}

void sd_features_register_mcp_tools()
{
    if (!sd_card::isMounted()) {
        ESP_LOGW(TAG, "No SD card: voice, story and skin tools not registered");
        return;
    }
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.voice.record_sample",
                "Record a voice sample of a child for voice recognition enrollment, saved on the SD card. Use when a "
                "child asks to record or register their voice. Recording starts right after you finish speaking: "
                "tell the child to say a few sentences after you stop talking. Ask for their name first.",
                PropertyList({Property("name", kPropertyTypeString),
                              Property("seconds", kPropertyTypeInteger, 8, 3, 20)}),
                [](const PropertyList& properties) -> ReturnValue {
                    auto name   = properties["name"].value<std::string>();
                    int seconds = properties["seconds"].value<int>();
                    if (!sd_recorder::arm(name, seconds)) {
                        return std::string("Cannot record now (another recording is running, or invalid name).");
                    }
                    return std::string("Recording will start when you stop talking and last ") +
                           std::to_string(seconds) + " seconds.";
                });

    mcp.AddTool("self.story.list",
                "List ALL the stories the robot has: stories you READ ALOUD (3-5 min, the main ones) and recorded "
                "audio stories/songs on the SD card. Use whenever the children ask for a story.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    return std::string("To read aloud: ") + stories::listTitles() +
                           ". Recorded audio: " + json_list(sd_story::list());
                });

    mcp.AddTool("self.story.play",
                "Tell or play a story (names from self.story.list; partial names work). For read-aloud stories it "
                "returns page 1 with the reading rules: read it and continue with self.historia.continuar. For "
                "recorded audio it starts when you finish talking.",
                PropertyList({Property("name", kPropertyTypeString)}),
                [](const PropertyList& properties) -> ReturnValue {
                    std::string name = properties["name"].value<std::string>();
                    inner_state::onEvent(inner_state::Event::Play);
                    // Read-aloud stories first: they return page 1 for the AI to read
                    std::string reading;
                    if (stories::start(name, reading, false)) {
                        return reading;
                    }
                    if (!sd_story::request(name)) {
                        // Nothing matched: read a random story rather than disappoint
                        stories::start(name, reading, true);
                        return reading;
                    }
                    return std::string("Recorded audio story queued: it starts when you finish talking.");
                });

    mcp.AddTool("self.story.stop", "Stop the story or song that is playing.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    sd_story::stop();
                    return true;
                });

    // One tool for the skins: the server accepts at most 32 tools in all
    mcp.AddTool("self.skin",
                "Robot face skins on the SD card. action: list, or set (name from the list; \"padrao\" is the "
                "built-in face). After set the robot restarts a few seconds later to apply it; tell the child.",
                PropertyList({Property("action", kPropertyTypeString),
                              Property("name", kPropertyTypeString, std::string(""))}),
                [](const PropertyList& properties) -> ReturnValue {
                    std::string action = properties["action"].value<std::string>();
                    std::string name   = properties["name"].value<std::string>();
                    if (action != "set" || name.empty()) {
                        auto skins = sd_skin::list();
                        skins.insert(skins.begin(), "padrao");
                        return json_list(skins);
                    }
                    if (!sd_skin::setActive(name)) {
                        return std::string("Skin not found. Skins: ") + json_list(sd_skin::list());
                    }
                    // Give the answer time to be spoken, then restart into the new face
                    xTaskCreate(
                        [](void*) {
                            vTaskDelay(pdMS_TO_TICKS(8000));
                            esp_restart();
                        },
                        "skin_restart", 2048, nullptr, 1, nullptr);
                    return true;
                });

    // Long-term memory (graph of facts) on the card
    memory_graph::registerMcpTool();

    ESP_LOGI(TAG, "SD card tools registered");
}
