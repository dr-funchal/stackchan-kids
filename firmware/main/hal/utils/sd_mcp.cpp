/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_features.h"
#include "sd_card.h"
#include "ir_remote.h"
#include "stories.h"
#include "gateway_audio.h"
#include <stackchan/avatar/skins/sd/sd_skin.h>
#include <application.h>
#include <assets/lang_config.h>
#include <freertos/idf_additions.h>
#include <atomic>
#include <memory>
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

// IR learning waits for the remote, so it runs in its own task and answers with a sound
struct IrLearnJob {
    std::string name;
    int seconds;
};
static std::atomic<bool> _ir_learning{false};

static void ir_learn(const IrLearnJob* job)
{
    std::string error;
    bool ok = ir_remote::learn(job->name, job->seconds * 1000, error);
    if (!ok) {
        ESP_LOGW(TAG, "IR learn failed: %s", error.c_str());
    }
    sd_diary::log("sistema", ((ok ? "controle aprendido: " : "controle NAO aprendido: ") + job->name).c_str());
    Application::GetInstance().Schedule(
        [ok]() { Application::GetInstance().PlaySound(ok ? Lang::Sounds::OGG_SUCCESS : Lang::Sounds::OGG_EXCLAMATION); });
}

static void ir_learn_task(void* arg)
{
    {
        std::unique_ptr<IrLearnJob> job(static_cast<IrLearnJob*>(arg));
        ir_learn(job.get());
    }  // vTaskDeleteWithCaps never returns: free everything before it
    _ir_learning.store(false);
    vTaskDeleteWithCaps(nullptr);
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

    mcp.AddTool("self.skin.list", "List the face skins on the SD card. \"padrao\" is the built-in face.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    auto skins = sd_skin::list();
                    skins.insert(skins.begin(), "padrao");
                    return json_list(skins);
                });

    mcp.AddTool("self.skin.set",
                "Change the robot's face to a skin from the SD card (\"padrao\" for the built-in face). The robot "
                "restarts a few seconds later to apply it; tell the child.",
                PropertyList({Property("name", kPropertyTypeString)}),
                [](const PropertyList& properties) -> ReturnValue {
                    if (!sd_skin::setActive(properties["name"].value<std::string>())) {
                        return std::string("Skin not found.");
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

    // One tool for the remote: the server accepts at most 32 tools in all
    mcp.AddTool("self.ir",
                "Infrared remote control (TV, air conditioner, fan, sound system). action: send (press a learned "
                "button: turn the TV on/off, volume...; partial names work), list (learned buttons), learn (learn a "
                "new button; use a short name like \"tv_ligar\", \"tv_volume_mais\", \"ar_desligar\"; then tell the "
                "person to point the remote at your body and press the button now: a chime means learned, an alert "
                "sound means nothing was received). name = the button.",
                PropertyList({Property("action", kPropertyTypeString),
                              Property("name", kPropertyTypeString, std::string(""))}),
                [](const PropertyList& properties) -> ReturnValue {
                    std::string action = properties["action"].value<std::string>();
                    std::string name   = properties["name"].value<std::string>();
                    if (action == "list" || (action == "send" && name.empty())) {
                        return json_list(ir_remote::list());
                    }
                    if (action == "send") {
                        std::string error;
                        if (!ir_remote::send(name, error)) {
                            return error + ". Learned buttons: " + json_list(ir_remote::list());
                        }
                        return true;
                    }
                    if (action != "learn" || name.empty()) {
                        return std::string("Use action send, list or learn (learn needs a name).");
                    }
                    if (_ir_learning.exchange(true)) {
                        return std::string("Already waiting for a remote button.");
                    }
                    auto* job = new IrLearnJob{name, 15};
                    if (xTaskCreatePinnedToCoreWithCaps(ir_learn_task, "ir_learn", 6144, job, 3, nullptr,
                                                        tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS) {
                        delete job;
                        _ir_learning.store(false);
                        return std::string("Cannot start learning.");
                    }
                    return std::string("Waiting for the remote button now.");
                });

    gateway_audio::registerMcpTools();

    ESP_LOGI(TAG, "SD card tools registered");
}
