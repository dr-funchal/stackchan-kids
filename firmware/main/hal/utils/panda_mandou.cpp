/*
 * SPDX-License-Identifier: MIT
 */
#include "panda_mandou.h"
#include "sd_features.h"
#include <hal/hal.h>
#include <hal/board/stackchan_camera.h>
#include <stackchan/stackchan.h>
#include <application.h>
#include <board.h>
#include <mcp_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

using namespace stackchan;

static const char* TAG = "PandaMandou";

/* ---------------------------------- State -------------------------------- */

enum class Action { None, HeadTouch, Pet, Shake, ScreenTouch, Color };

namespace {
struct Player {
    std::string name;
    int stars = 0;
};

struct Game {
    bool active = false;
    std::vector<Player> players;
    size_t turn = 0;
    int rounds  = 0;
};

Game _game;
std::atomic<bool> _active{false};

// The armed challenge. Detectors run on other tasks, so the outcome is claimed with an atomic exchange
std::atomic<int> _armed{(int)Action::None};
std::atomic<uint32_t> _challenge_id{0};
bool _panda_said     = true;
std::string _color;  // Target color name for Action::Color
uint32_t _deadline_ms = 0;
std::atomic<bool> _resolved{true};
}  // namespace

bool panda_mandou::isActive()
{
    return _active.load();
}

static std::string current_player()
{
    return _game.players.empty() ? std::string("todos") : _game.players[_game.turn].name;
}

static std::string scoreboard()
{
    std::string text;
    for (auto& p : _game.players) {
        text += (text.empty() ? "" : ", ") + p.name + ": " + std::to_string(p.stars) + " stars";
    }
    return text.empty() ? "no players" : text;
}

/* -------------------------------- Colors --------------------------------- */

struct NamedColor {
    const char* name;
    uint32_t rgb;  // For the on-screen swatch
};

static const NamedColor kColors[] = {
    {"vermelho", 0xE53935}, {"laranja", 0xFB8C00}, {"amarelo", 0xFDD835}, {"verde", 0x43A047},
    {"azul", 0x1E88E5},     {"roxo", 0x8E24AA},    {"rosa", 0xF06292},    {"branco", 0xFFFFFF},
    {"preto", 0x212121},
};

static const NamedColor* find_color(const std::string& name)
{
    std::string key = sd_paths::sanitize(name);
    for (auto& c : kColors) {
        if (key.find(c.name) != std::string::npos) {
            return &c;
        }
    }
    if (key.find("lilas") != std::string::npos) {
        return &kColors[5];
    }
    return nullptr;
}

// Share of the central region that looks like `target`, from an RGB565 frame. Pixels too dull or dark only
// count for white/black. Rough on purpose: home lighting and the camera's white balance vary a lot
static int color_share(const uint16_t* px, int width, int height, const char* target)
{
    int hits = 0, total = 0;
    int x0 = width / 2 - 50, y0 = height / 2 - 50;
    for (int y = y0; y < y0 + 100; y += 3) {
        for (int x = x0; x < x0 + 100; x += 3) {
            uint16_t v = px[y * width + x];
            float r = ((v >> 11) & 0x1F) / 31.0f, g = ((v >> 5) & 0x3F) / 63.0f, b = (v & 0x1F) / 31.0f;
            float mx = std::max({r, g, b}), mn = std::min({r, g, b});
            float sat = mx > 0 ? (mx - mn) / mx : 0;
            float hue = 0;
            if (mx > mn) {
                if (mx == r) {
                    hue = 60 * fmodf((g - b) / (mx - mn), 6);
                } else if (mx == g) {
                    hue = 60 * ((b - r) / (mx - mn) + 2);
                } else {
                    hue = 60 * ((r - g) / (mx - mn) + 4);
                }
                if (hue < 0) {
                    hue += 360;
                }
            }
            const char* name;
            if (mx < 0.18f) {
                name = "preto";
            } else if (sat < 0.25f) {
                name = mx > 0.7f ? "branco" : "";
            } else if (hue < 15 || hue >= 340) {
                name = "vermelho";
            } else if (hue < 40) {
                name = "laranja";
            } else if (hue < 70) {
                name = "amarelo";
            } else if (hue < 170) {
                name = "verde";
            } else if (hue < 255) {
                name = "azul";
            } else if (hue < 295) {
                name = "roxo";
            } else {
                name = "rosa";
            }
            hits += strcmp(name, target) == 0;
            total++;
        }
    }
    return total ? hits * 100 / total : 0;
}

/* ----------------------------------- UI ---------------------------------- */

// Big color dot so kids who can't read the name still know what to bring. Top layer, caller holds the lock
static void show_swatch(const NamedColor* color)
{
    static lv_obj_t* dot = nullptr;
    if (!color) {
        if (dot) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (!dot) {
        dot = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 84, 84);
        lv_obj_align(dot, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 5, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x141018), 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_style_bg_color(dot, lv_color_hex(color->rgb), 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

static void react(bool good)
{
    LvglLockGuard lock;
    GetStackChan().addModifier(
        std::make_unique<GestureModifier>(good ? GestureModifier::Kind::Happy : GestureModifier::Kind::Surprised));
}

/* --------------------------------- Rules --------------------------------- */

// Called once per challenge with what the child did (Action::None = nothing before the clock ran out)
static void resolve(Action done)
{
    if (_resolved.exchange(true)) {
        return;  // Already resolved by another detector or the clock
    }
    Action wanted = (Action)_armed.exchange((int)Action::None);
    {
        LvglLockGuard lock;
        show_swatch(nullptr);
    }

    bool did_it  = done == wanted;
    bool correct = _panda_said ? did_it : done == Action::None;
    std::string who = current_player();
    if (correct && !_game.players.empty()) {
        _game.players[_game.turn].stars++;
    }
    _game.rounds++;
    if (!_game.players.empty()) {
        _game.turn = (_game.turn + 1) % _game.players.size();
    }
    react(correct);

    // Tell the AI, as if spoken. Rules explain this "(sensores: ...)" format
    std::string what;
    if (wanted == Action::Color) {
        what = did_it ? "a camera viu algo " + _color : "a camera nao viu nada " + _color;
    } else {
        what = did_it ? "a crianca fez o comando" : "ninguem fez o comando";
    }
    std::string text = "(sensores: " + what + ". " + who + (correct ? " acertou" : " errou") +
                       (_panda_said ? "" : ", era pegadinha") + ". Proximo: " + current_player() + ")";
    ESP_LOGI(TAG, "%s", text.c_str());
    sd_diary::log("panda-mandou", text.c_str());
    Application::GetInstance().SendUserText(text);
}

/* ------------------------------- Detectors ------------------------------- */

static void on_action(Action action)
{
    if (_resolved.load() || (Action)_armed.load() == Action::None) {
        return;
    }
    // Any action counts: doing the wrong thing on a trick still "falls for it", and a different action than
    // asked is just ignored on a real command
    Action wanted = (Action)_armed.load();
    if (action == wanted || !_panda_said) {
        resolve(action == wanted ? action : wanted);
    }
}

bool panda_mandou::onScreenTap()
{
    if (!_active.load() || _resolved.load()) {
        return false;
    }
    on_action(Action::ScreenTouch);
    return true;  // Don't let the tap close the conversation mid-game
}

// Clock on the body LEDs + camera polling for color challenges
static void challenge_task(void* arg)
{
    uint32_t id      = (uint32_t)(uintptr_t)arg;
    uint32_t total   = _deadline_ms - GetHAL().millis();
    int last_lit     = -1;
    int matches      = 0;
    uint32_t next_cam = 0;
    auto camera       = dynamic_cast<StackChanCamera*>(Board::GetInstance().GetCamera());

    while (!_resolved.load() && _challenge_id.load() == id) {
        uint32_t now = GetHAL().millis();
        if ((int32_t)(now - _deadline_ms) >= 0) {
            resolve(Action::None);
            break;
        }

        int lit = 1 + (int)((_deadline_ms - now) * 11 / total);
        if (lit != last_lit) {
            last_lit = lit;
            uint8_t r = lit > 5 ? 0 : 40, g = lit > 2 ? 30 : 0;
            for (uint8_t i = 1; i < 12; i++) {
                GetHAL().setRgbColor(i, i <= lit ? r : 0, i <= lit ? g : 0, 0);
            }
            GetHAL().refreshRgb();
        }

        if ((Action)_armed.load() == Action::Color && camera && now >= next_cam) {
            next_cam = now + 800;
            if (camera->Capture() && camera->GetFrameFormat() == V4L2_PIX_FMT_RGB565 &&
                camera->GetFrameWidth() >= 120 && camera->GetFrameHeight() >= 120) {
                int share = color_share((const uint16_t*)camera->GetFrameData(), camera->GetFrameWidth(),
                                        camera->GetFrameHeight(), _color.c_str());
                ESP_LOGI(TAG, "Camera: %d%% %s", share, _color.c_str());
                matches = share >= 22 ? matches + 1 : 0;
                if (matches >= 2) {  // Two frames in a row: the object is really there
                    resolve(Action::Color);
                    break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    for (uint8_t i = 1; i < 12; i++) {
        GetHAL().setRgbColor(i, 0, 0, 0);
    }
    GetHAL().refreshRgb();
    vTaskDeleteWithCaps(nullptr);
}

static void subscribe_sensors_once()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    GetHAL().onHeadPetGesture.connect([](HeadPetGesture gesture) {
        if (gesture == HeadPetGesture::Press) {
            on_action(Action::HeadTouch);
        } else if (gesture == HeadPetGesture::SwipeForward || gesture == HeadPetGesture::SwipeBackward) {
            on_action(Action::Pet);
        }
    });
    GetHAL().onImuMotionEvent.connect([](ImuMotionEvent event) {
        if (event == ImuMotionEvent::Shake) {
            on_action(Action::Shake);
        }
    });
}

/* ---------------------------------- Tools -------------------------------- */

static const char* kRules =
    "O PANDA MANDOU RULES (children 4-6, Portuguese, turns). Call the child by name and give ONE command with "
    "self.mandou.command BEFORE saying it. Most commands start with \"o panda mandou\" (panda_said=true); about 1 "
    "in 4 is a TRICK without it (panda_said=false): then the child must NOT do it. Actions: \"cabeca\" (touch my "
    "head), \"carinho\" (pet my head), \"chacoalhar\" (shake me gently), \"rosto\" (touch my face on the screen), "
    "\"cor\" (bring something of a color and show it to my camera; a big dot of that color shows on my screen; "
    "use simple colors: vermelho, laranja, amarelo, verde, azul, roxo, rosa, branco, preto). Mix color hunts with "
    "body commands. The robot checks with its sensors and, when done or when the clock ends, you receive a "
    "message starting with \"(sensores: ...)\": it is NOT the child talking, it is the result. Then cheer (or "
    "laugh kindly at a trick) and give the next command to the next child named in it. Never judge yourself what "
    "the child did: only the sensors message counts. Short joyful sentences. Stop with self.mandou.end.";

void panda_mandou::registerMcpTools()
{
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.mandou.start",
                "Start the game \"O Panda Mandou\" (Simon says + color hunt). Ask the players' names first. Returns "
                "the rules and who goes first.",
                PropertyList({Property("players", kPropertyTypeString)}),
                [](const PropertyList& properties) -> ReturnValue {
                    subscribe_sensors_once();
                    _game = Game();
                    std::string names = properties["players"].value<std::string>();
                    std::string cur;
                    for (char c : names + ",") {
                        if (c == ',' || c == ';') {
                            while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
                            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                            size_t e = cur.find(" e ");
                            while (e != std::string::npos) {
                                _game.players.push_back({cur.substr(0, e), 0});
                                cur = cur.substr(e + 3);
                                e   = cur.find(" e ");
                            }
                            if (!cur.empty()) {
                                _game.players.push_back({cur, 0});
                            }
                            cur.clear();
                        } else {
                            cur += c;
                        }
                    }
                    _game.active = true;
                    _active.store(true);
                    sd_diary::log("panda-mandou", ("inicio: " + names).c_str());
                    ESP_LOGI(TAG, "Start: %u players", (unsigned)_game.players.size());
                    return std::string(kRules) + " Players: " + scoreboard() + ". First: " + current_player() + ".";
                });

    mcp.AddTool("self.mandou.command",
                "O Panda Mandou: arm the robot's sensors for the command you are about to say. action: cabeca, "
                "carinho, chacoalhar, rosto or cor (then set color). panda_said=false for a trick command.",
                PropertyList({Property("action", kPropertyTypeString), Property("color", kPropertyTypeString, ""),
                              Property("panda_said", kPropertyTypeBoolean, true)}),
                [](const PropertyList& properties) -> ReturnValue {
                    if (!_game.active) {
                        return std::string("No game running. Use self.mandou.start.");
                    }
                    std::string a = sd_paths::sanitize(properties["action"].value<std::string>());
                    Action action = a.find("cabec") != std::string::npos   ? Action::HeadTouch
                                    : a.find("carinh") != std::string::npos ? Action::Pet
                                    : a.find("chacoal") != std::string::npos || a.find("balanc") != std::string::npos
                                        ? Action::Shake
                                    : a.find("rosto") != std::string::npos || a.find("tela") != std::string::npos
                                        ? Action::ScreenTouch
                                    : a.find("cor") != std::string::npos ? Action::Color
                                                                          : Action::None;
                    if (action == Action::None) {
                        return std::string("Unknown action. Use cabeca, carinho, chacoalhar, rosto or cor.");
                    }
                    const NamedColor* color = nullptr;
                    if (action == Action::Color) {
                        color = find_color(properties["color"].value<std::string>());
                        if (!color) {
                            return std::string("Pick one of: vermelho, laranja, amarelo, verde, azul, roxo, rosa, "
                                               "branco, preto.");
                        }
                        _color = color->name;
                    }
                    _panda_said  = properties["panda_said"].value<bool>();
                    _deadline_ms = GetHAL().millis() + (action == Action::Color ? 25000 : 15000);
                    uint32_t id  = ++_challenge_id;
                    _armed.store((int)action);
                    _resolved.store(false);
                    {
                        LvglLockGuard lock;
                        show_swatch(color);
                    }
                    xTaskCreatePinnedToCoreWithCaps(challenge_task, "mandou", 6144, (void*)(uintptr_t)id, 3,
                                                    nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
                    return std::string("Sensors armed for ") + current_player() +
                           ". Now say the command. Wait for the (sensores: ...) message before judging.";
                });

    mcp.AddTool("self.mandou.end", "O Panda Mandou: stop the game and announce the stars.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    if (!_game.active) {
                        return std::string("No game running.");
                    }
                    _resolved.store(true);
                    _armed.store((int)Action::None);
                    _game.active = false;
                    _active.store(false);
                    {
                        LvglLockGuard lock;
                        show_swatch(nullptr);
                        GetStackChan().addModifier(std::make_unique<PartyModifier>());
                        GetStackChan().addModifier(std::make_unique<DanceModifier>(DanceModifier::Happy));
                    }
                    std::string summary = "Game over after " + std::to_string(_game.rounds) + " commands. " +
                                          scoreboard() + ". Celebrate everyone by name, no losers.";
                    sd_diary::log("panda-mandou", summary.c_str());
                    return summary;
                });

    ESP_LOGI(TAG, "Game tools registered");
}
