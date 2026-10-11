/*
 * SPDX-License-Identifier: MIT
 */
#include "papa_letras.h"
#include "sd_features.h"
#include "sd_images.h"
#include "sound_crunch.h"
#include "sound_fanfare.h"
#include <application.h>
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <stackchan/inner_state/inner_state.h>
#include <mcp_server.h>
#include <esp_log.h>
#include <lvgl.h>
#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <vector>

using namespace stackchan;

static const char* TAG = "PapaLetras";

// Letters for 4-6 year olds (no K, Q, W, X, Y). Images on the SD card (imagens/letras/), made by
// assets_src/papa_letras/build_letters.py
static const char kLetters[] = "ABCDEFGHIJLMNOPRSTUVZ";



static constexpr int kMaxTries = 2;  // Wrong answers on a letter before the AI may help with the answer

namespace {
struct Player {
    std::string name;
    int stars = 0;
};

struct Game {
    bool active = false;
    uint32_t paused_at = 0;  // millis() when stopped; the game can be resumed for a while
    std::string theme;
    std::vector<Player> players;
    size_t letter     = 0;  // Index into kLetters
    size_t turn       = 0;  // Index into players
    int tries         = 0;  // Wrong answers on the current letter
    int hints         = 0;
    int skipped       = 0;
    std::set<std::string> used_words;
};

Game _game;
std::atomic<bool> _active{false};
lv_obj_t* _letter_image = nullptr;
}  // namespace

bool papa_letras::isActive()
{
    return _active.load();
}


/* ----------------------------------- UI ---------------------------------- */

// Big letter between the eyes, above the face (top layer, so it survives face redraws)
static constexpr int kLetterTopY   = 6;
static constexpr int kLetterMouthY = 106;  // Letter center on the mouth (160,154) with a 96 px image

static void anim_y(void* obj, int32_t v)
{
    lv_obj_set_y(static_cast<lv_obj_t*>(obj), v);
}

static void anim_scale(void* obj, int32_t v)
{
    lv_image_set_scale(static_cast<lv_obj_t*>(obj), v);
}

static void run_anim(lv_obj_t* obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t ms,
                     lv_anim_path_cb_t path, uint32_t delay = 0, lv_anim_completed_cb_t done = nullptr,
                     void* user = nullptr)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    if (done) {
        lv_anim_set_completed_cb(&a, done);
        lv_anim_set_user_data(&a, user);
    }
    lv_anim_start(&a);
}

// New letter pops in with a springy bounce. Caller holds the LVGL lock
static void pop_in(int index)
{
    if (!_letter_image) {
        _letter_image = lv_image_create(lv_layer_top());
        lv_obj_align(_letter_image, LV_ALIGN_TOP_MID, 0, kLetterTopY);
    }
    lv_anim_delete(_letter_image, nullptr);
    std::string name = std::string("letras/papa_letra_") + kLetters[index];
    lv_image_set_src(_letter_image, sd_images::get(name.c_str()));  // No card: no letter, the game still works
    lv_obj_set_y(_letter_image, kLetterTopY);
    // Never animate from scale 0: a zero-size transform made LVGL write out of bounds and corrupt the heap
    run_anim(_letter_image, anim_scale, 40, LV_SCALE_NONE, 380, lv_anim_path_overshoot);
}

static void show_letter(int index)
{
    LvglLockGuard lock;
    if (index < 0) {
        if (_letter_image) {
            lv_anim_delete(_letter_image, nullptr);
            lv_obj_delete(_letter_image);
            _letter_image = nullptr;
        }
        return;
    }
    pop_in(index);
}

// The letter drops into the panda's mouth while shrinking, then the next one pops in (or none at the end)
static void eat_letter(int next)
{
    LvglLockGuard lock;
    if (!_letter_image) {
        if (next >= 0) {
            pop_in(next);
        }
        return;
    }
    lv_anim_delete(_letter_image, nullptr);
    run_anim(_letter_image, anim_y, kLetterTopY, kLetterMouthY, 550, lv_anim_path_ease_in);
    run_anim(_letter_image, anim_scale, LV_SCALE_NONE, 24, 550, lv_anim_path_ease_in, 0,
             [](lv_anim_t* a) {
                 int next = (int)(intptr_t)lv_anim_get_user_data(a);
                 if (next >= 0) {
                     pop_in(next);
                 } else if (_letter_image) {
                     lv_obj_delete_async(_letter_image);
                     _letter_image = nullptr;
                 }
             },
             (void*)(intptr_t)next);
}

static void play_sound(const uint8_t* data, size_t size)
{
    Application::GetInstance().PlaySound(std::string_view(reinterpret_cast<const char*>(data), size));
}

// Turn clock: the 11 body LEDs light up when it's a child's turn and go out one by one over 20 s, green ->
// yellow -> red. At zero they blink red and the AI "hears" that time is up. A new turn or an answer stops it:
// each clock carries the generation it was started with and destroys itself when that changes
static constexpr uint32_t kTurnMs = 20 * 1000;
static std::atomic<uint32_t> _clock_generation{0};
// Time-ups in a row with no answer: after 2 the clock stops talking, so a room the kids left goes quiet
static std::atomic<int> _timeouts_in_a_row{0};
static constexpr int kMaxTimeoutsInARow = 2;

class TurnClockModifier : public Modifier {
public:
    explicit TurnClockModifier(uint32_t generation) : _generation(generation) {}

    void _update(Modifiable&) override
    {
        uint32_t now = GetHAL().millis();
        if (_start == 0) {
            _start = now;
        }
        if (_clock_generation.load() != _generation || !_active.load()) {
            set_all(0, 0, 0);
            requestDestroy();
            return;
        }

        uint32_t elapsed = now - _start;
        if (elapsed < kTurnMs) {
            int lit = 11 - (int)(elapsed * 11 / kTurnMs);  // 11 .. 1
            if (lit != _last_lit) {
                _last_lit = lit;
                uint8_t r = lit > 5 ? 0 : 40, g = lit > 2 ? 30 : 0;  // green, yellow, red
                for (uint8_t i = 1; i < 12; i++) {
                    bool on = i <= lit;
                    GetHAL().setRgbColor(i, on ? r : 0, on ? g : 0, 0);
                }
                GetHAL().refreshRgb();
            }
            return;
        }

        // Time is up: blink red three times, then tell the AI
        int blink = (elapsed - kTurnMs) / 250;
        if (blink < 6) {
            if (blink != _last_blink) {
                _last_blink = blink;
                set_all(blink % 2 ? 0 : 50, 0, 0);
            }
            return;
        }
        set_all(0, 0, 0);
        if (_timeouts_in_a_row.fetch_add(1) < kMaxTimeoutsInARow) {
            ESP_LOGI(TAG, "Turn time is up");
            Application::GetInstance().SendUserText("O tempo acabou!");
        }
        requestDestroy();
    }

private:
    static void set_all(uint8_t r, uint8_t g, uint8_t b)
    {
        for (uint8_t i = 1; i < 12; i++) {
            GetHAL().setRgbColor(i, r, g, b);
        }
        GetHAL().refreshRgb();
    }

    uint32_t _generation;
    uint32_t _start = 0;
    int _last_lit   = -1;
    int _last_blink = -1;
};

void papa_letras::onListening(bool listening)
{
    uint32_t generation = ++_clock_generation;  // Any status change ends the running clock
    if (listening && _active.load()) {
        GetStackChan().addModifier(std::make_unique<TurnClockModifier>(generation));  // Caller holds the lock
    }
}

void papa_letras::onConversationEnded()
{
    if (!_game.active) {
        return;
    }
    // The AI said goodbye (or the room went quiet) without self.game.end: pause quietly, no fanfare
    _game.active    = false;
    _game.paused_at = GetHAL().millis();
    _active.store(false);
    ++_clock_generation;
    show_letter(-1);
    ESP_LOGI(TAG, "Conversation ended mid-game: paused at letter %c", kLetters[_game.letter]);
    sd_diary::log("papa-letras", (std::string("pausado na letra ") + kLetters[_game.letter]).c_str());
}

void papa_letras::onHeadTap()
{
    if (_active.load()) {
        ESP_LOGI(TAG, "Head tap: hint");
        Application::GetInstance().SendUserText("Me da uma dica!");
    }
}

void papa_letras::onHeadPet()
{
    if (_active.load()) {
        ESP_LOGI(TAG, "Head pet: skip");
        Application::GetInstance().SendUserText("Pula essa letra!");
    }
}

static void celebrate()
{
    LvglLockGuard lock;
    GetStackChan().addModifier(std::make_unique<GestureModifier>(GestureModifier::Kind::Happy));
}

static void party()
{
    LvglLockGuard lock;
    GetStackChan().addModifier(std::make_unique<PartyModifier>());
    GetStackChan().addModifier(std::make_unique<DanceModifier>(DanceModifier::Happy));
}

/* --------------------------------- Rules --------------------------------- */

static char current_letter()
{
    return kLetters[_game.letter];
}

static std::string current_player()
{
    return _game.players.empty() ? std::string("todos") : _game.players[_game.turn].name;
}

static std::string turn_text()
{
    return std::string("Letter ") + current_letter() + ", turn of " + current_player() + ".";
}

static std::string scoreboard()
{
    std::string text;
    for (auto& p : _game.players) {
        text += (text.empty() ? "" : ", ") + p.name + ": " + std::to_string(p.stars) + " stars";
    }
    return text.empty() ? "no players" : text;
}

// Next letter and next player; false when the alphabet is done
static bool advance()
{
    _game.tries = 0;
    _game.letter++;
    if (!_game.players.empty()) {
        _game.turn = (_game.turn + 1) % _game.players.size();
    }
    if (_game.letter >= sizeof(kLetters) - 1) {
        eat_letter(-1);
        return false;
    }
    eat_letter(_game.letter);
    return true;
}

static constexpr uint32_t kResumeWindowMs = 30 * 60 * 1000;

static std::string finish()
{
    _game.active    = false;
    _game.paused_at = GetHAL().millis();
    _active.store(false);
    show_letter(-1);
    int total = 0;
    for (auto& p : _game.players) {
        total += p.stars;
    }
    std::string summary = "Game over! Theme " + _game.theme + ". The panda ate " + std::to_string(total) +
                          " letters. Scores: " + scoreboard() +
                          ". Celebrate everyone warmly by name; there are no losers at this age. If they want to go on later, "
                          "self.game.resume continues from this letter.";
    sd_diary::log("papa-letras", summary.c_str());
    play_sound(kSound_fanfare, kSound_fanfare_size);
    party();
    ESP_LOGI(TAG, "%s", summary.c_str());
    return summary;
}

// Hint guideline. The AI tends to hint words of another letter ("tromba" for A), so it must pick and check first
static std::string hint_instruction()
{
    return std::string("HINT STEPS: 1) Silently pick a ") + _game.theme + " whose name starts with \"" +
           current_letter() + "\". 2) Spell its first letter in your head; if it is not \"" + current_letter() +
           "\", pick another. 3) Vary: not a word already used in this game, nor one you already hinted. "
           "4) Start the hint with the sound (\"comeca com o som de " + current_letter() +
           "...\") and give ONE clue, broad first (where it lives, color, size), more specific on the next try. "
           "5) Never say the word itself.";
}

static std::vector<std::string> split_names(const std::string& text)
{
    std::vector<std::string> names;
    std::string current;
    auto flush = [&]() {
        while (!current.empty() && current.front() == ' ') {
            current.erase(current.begin());
        }
        while (!current.empty() && current.back() == ' ') {
            current.pop_back();
        }
        if (!current.empty()) {
            names.push_back(current);
        }
        current.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ';') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    // " e " separates the last name in Portuguese ("Gui e Lia")
    std::vector<std::string> out;
    for (auto& n : names) {
        size_t pos;
        std::string rest = n;
        while ((pos = rest.find(" e ")) != std::string::npos) {
            out.push_back(rest.substr(0, pos));
            rest = rest.substr(pos + 3);
        }
        out.push_back(rest);
    }
    return out;
}

/* ---------------------------------- Tools -------------------------------- */

static const char* kRules =
    "PAPA-LETRAS RULES (children aged 4-6, Portuguese). MANDATORY PROTOCOL: (1) After EVERY answer a child gives, "
    "your FIRST action is to call self.game.answer(word, valid) BEFORE you say anything about it. (2) NEVER announce "
    "the next letter or whose turn it is by yourself: only say what self.game.answer, self.game.skip or "
    "self.game.resume return. The letter on the robot screen only changes through these tools. (3) NEVER say a "
    "word that answers the current letter during the first 2 tries: no examples, no lists. Hints only describe "
    "(sound, color, size, what it does, where it lives). (4) A single short word like \"tchau\", \"para\" or an "
    "unclear phrase does NOT end the game: ask \"vocês querem parar o jogo?\" and after a clear yes you MUST call "
    "self.game.end (saying the game stopped is not enough: the turn clock keeps running until you call it). HOW TO PLAY: say the letter and its sound, call the child by name, they say a word of the theme "
    "starting with the letter. (5) UNDERSTAND CHILD SPEECH BY SOUND: these are 4-6 year olds and the transcript "
    "often has their pronunciation, not the real word. Before judging, find the theme word that SOUNDS closest. "
    "Typical patterns in Portuguese: R becomes L (jacale=jacare, laposa=raposa), R dropped in clusters "
    "(bincar=brincar, tigue=tigre), S/Z become X/CH (xapo=sapo, xebla=zebra), LH becomes L or I (abelia=abelha, "
    "coeio=coelho), swapped or repeated syllables (cacaco=macaco, bolboleta=borboleta), cut endings "
    "(elefan=elefante), plus speech-recognition slips (a beija=abelha). If it sounds like a valid word, ACCEPT it: "
    "pass the CORRECT word to self.game.answer and repeat it right, warmly (\"Isso! Jacare!\"), never point out "
    "the mistake. If you are really unsure, ask \"voce disse jacare?\". The first SOUND matters. (6) The robot also sends "
    "these on its own: \"O tempo acabou!\" (the 20 s turn clock ran out: encourage, give a hint via self.game.hint, "
    "or call self.game.skip if it was already the 2nd try), \"Me da uma dica!\" (head tap: call self.game.hint) and "
    "\"Pula essa letra!\" (head petting: call self.game.skip). If wrong, encourage and give a tiny hint. If the child asks for help call self.game.hint; if nobody "
    "knows after 2 tries call self.game.skip. Short, joyful sentences; cheer the panda eating the letter "
    "(nham nham!).";

void papa_letras::registerMcpTools()
{
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.game.start",
                "Start the Papa-Letras word game. Call when the children want to play Papa-Letras (letter eating "
                "game). Ask the players' names first if you don't know them. If they don't choose a theme, pick one "
                "fit for 4-6 year olds: animais, frutas, comidas, brinquedos, cores, coisas da casa, nomes, "
                "profissoes. Returns the rules and the first turn.",
                PropertyList({Property("theme", kPropertyTypeString), Property("players", kPropertyTypeString)}),
                [](const PropertyList& properties) -> ReturnValue {
                    _game          = Game();
                    _game.theme    = properties["theme"].value<std::string>();
                    for (auto& name : split_names(properties["players"].value<std::string>())) {
                        _game.players.push_back({name, 0});
                    }
                    _game.active = true;
                    _active.store(true);
                    _timeouts_in_a_row.store(0);
                    inner_state::onEvent(inner_state::Event::Play);
                    show_letter(0);
                    ESP_LOGI(TAG, "Start: theme %s, %u players", _game.theme.c_str(), (unsigned)_game.players.size());
                    sd_diary::log("papa-letras", ("inicio, tema " + _game.theme).c_str());
                    return std::string(kRules) + " Theme: " + _game.theme + ". Players in order: " +
                           scoreboard() + ". " + turn_text();
                });

    mcp.AddTool("self.game.answer",
                "Papa-Letras: MUST be called after EVERY child answer, before commenting it. word = the CORRECT word you "
                "understood by sound (\"jacale\" -> \"jacare\"). valid=true if it belongs to the theme and starts "
                "with the current letter. Returns the next letter and turn: never announce them yourself.",
                PropertyList({Property("word", kPropertyTypeString), Property("valid", kPropertyTypeBoolean)}),
                [](const PropertyList& properties) -> ReturnValue {
                    if (!_game.active) {
                        return std::string("No game running. Use self.game.start.");
                    }
                    _timeouts_in_a_row.store(0);
                    std::string word = properties["word"].value<std::string>();
                    bool valid       = properties["valid"].value<bool>();
                    std::string key  = sd_paths::sanitize(word);

                    if (valid && !key.empty() && (char)toupper(key[0]) != current_letter()) {
                        valid = false;  // The AI judged the theme; the first letter is checked here
                    }
                    if (valid && _game.used_words.count(key)) {
                        return std::string("\"") + word + "\" was already used! Ask " + current_player() +
                               " for another word. " + turn_text();
                    }
                    if (!valid) {
                        _game.tries++;
                        if (_game.tries >= kMaxTries) {
                            return std::string("Not this time. You may now tell ") + current_player() +
                                   " an easy example (check that it starts with " + current_letter() +
                                   "), then call self.game.skip. " + turn_text();
                        }
                        return std::string("Not accepted. Encourage ") + current_player() + ". " +
                               hint_instruction() + " Try " + std::to_string(_game.tries + 1) + " of " +
                               std::to_string(kMaxTries) + ". " + turn_text();
                    }

                    _game.used_words.insert(key);
                    if (!_game.players.empty()) {
                        _game.players[_game.turn].stars++;
                    }
                    std::string who = current_player();
                    play_sound(kSound_crunch, kSound_crunch_size);
                    celebrate();
                    if (!advance()) {
                        return std::string("Correct, star for ") + who + "! That was the last letter. " + finish();
                    }
                    return std::string("Correct! The panda eats the letter, star for ") + who + ". Next: " +
                           turn_text();
                });

    mcp.AddTool("self.game.hint", "Papa-Letras: the child asked for help on the current letter.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    if (!_game.active) {
                        return std::string("No game running.");
                    }
                    _game.hints++;
                    return hint_instruction() + " " + turn_text();
                });

    mcp.AddTool("self.game.skip", "Papa-Letras: nobody knows a word, go to the next letter.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    if (!_game.active) {
                        return std::string("No game running.");
                    }
                    _game.skipped++;
                    if (!advance()) {
                        return finish();
                    }
                    return std::string("The letter ran away! Next: ") + turn_text();
                });

    mcp.AddTool("self.game.status", "Papa-Letras: current letter, whose turn it is and the stars.", PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    if (!_game.active) {
                        bool resumable = !_game.theme.empty() && GetHAL().millis() - _game.paused_at <= kResumeWindowMs;
                        return std::string(resumable ? "The game is paused; call self.game.resume to continue it."
                                                     : "No game running.");
                    }
                    return std::string("Theme ") + _game.theme + ". " + turn_text() + " Stars: " + scoreboard();
                });

    mcp.AddTool("self.game.resume",
                "Papa-Letras: continue the last game from the same letter, keeping stars and used words. Use when "
                "the children say they want to continue the game.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    if (_game.active) {
                        return std::string("The game is already running. ") + turn_text();
                    }
                    if (_game.theme.empty() || GetHAL().millis() - _game.paused_at > kResumeWindowMs) {
                        return std::string("No game to continue. Start a new one with self.game.start.");
                    }
                    _game.active = true;
                    _active.store(true);
                    _timeouts_in_a_row.store(0);
                    show_letter(_game.letter);
                    return std::string(kRules) + " Continuing! Theme: " + _game.theme + ". Stars: " + scoreboard() +
                           ". " + turn_text();
                });

    mcp.AddTool("self.game.end",
                "Papa-Letras: stop the game, only after the children CLEARLY confirmed they want to stop (ask first "
                "if unsure). Announces the stars. It can be resumed later with self.game.resume.",
                PropertyList(),
                [](const PropertyList&) -> ReturnValue {
                    if (!_game.active) {
                        return std::string("No game running.");
                    }
                    return finish();
                });

    ESP_LOGI(TAG, "Game tools registered");
}
