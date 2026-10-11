/*
 * SPDX-License-Identifier: MIT
 */
#include "poker.h"
#include "poker_rules.h"
#include "sd_features.h"
#include "sound_fanfare.h"
#include <application.h>
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <stackchan/inner_state/inner_state.h>
#include <mcp_server.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <lvgl.h>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

using namespace stackchan;
using namespace poker_rules;

static const char* TAG = "Poker";

static constexpr int kStartChips = 20;
static constexpr int kAnte       = 1;
static constexpr int kMaxBet     = 5;

namespace {
enum class Phase {
    None,
    Draw,     // The child picks the cards to swap
    Bet,      // The child bets or checks
    Respond,  // The robot bet after a check: the child calls or folds
    Over,     // Hand finished, waiting for action next
};

struct Game {
    Phase phase = Phase::None;
    std::string player;
    int chips[2] = {kStartChips, kStartChips};  // 0 child, 1 robot
    int pot      = 0;
    int robot_bet = 0;
    int hands_played = 0;
    Card deck[52];
    int next_card = 0;
    Hand hand[2];
    bool swap[5] = {};
    bool revealed = false;  // The robot's cards are face up (showdown)
};

Game _game;
std::atomic<bool> _active{false};

lv_obj_t* _player_cards[5] = {};
lv_obj_t* _robot_cards[5]  = {};
lv_obj_t* _chips_label     = nullptr;
}  // namespace

bool poker::isActive()
{
    return _active.load();
}

/* --------------------------------- Deck ---------------------------------- */

static void shuffle()
{
    for (int i = 0; i < 52; i++) {
        _game.deck[i] = Card{(uint8_t)(i % 13 + 2), (uint8_t)(i / 13)};
    }
    for (int i = 51; i > 0; i--) {
        int j = esp_random() % (i + 1);
        std::swap(_game.deck[i], _game.deck[j]);
    }
    _game.next_card = 0;
}

static Card draw_card()
{
    return _game.deck[_game.next_card++];  // At most 20 of the 52 cards are used per hand
}

/* ---------------------------------- UI ----------------------------------- */
// Everything below runs with the LVGL lock held: the tools take it for their whole body and card taps arrive
// from the LVGL task, so the lock also guards the game state

// Suit symbols drawn at runtime (Montserrat has no ♥♦♠♣), 4x supersampled, ARGB8888 in PSRAM
static constexpr int kSuitBig   = 30;
static constexpr int kSuitSmall = 18;
static lv_image_dsc_t* _suit_images[2][4] = {};

static bool inside_heart(float x, float y)  // y up, heart centered near the origin
{
    float a = x * x + y * y - 1;
    return a * a * a - x * x * y * y * y <= 0;
}

static bool inside_suit(int suit, float u, float v)  // u, v in [-1, 1], v down
{
    switch (suit) {
        case 0:  // copas
            return inside_heart(u * 1.25f, -v * 1.25f + 0.2f);
        case 1:  // ouros
            return std::fabs(u) / 0.72f + std::fabs(v) <= 1;
        case 2:  // espadas: upside-down heart + stem
            if (inside_heart(u * 1.35f, v * 1.35f + 0.35f)) {
                return true;
            }
            return v > 0.3f && v < 0.95f && std::fabs(u) < 0.08f + (v - 0.3f) * 0.45f;
        default: {  // paus: three balls + stem
            auto ball = [&](float cx, float cy) { return (u - cx) * (u - cx) + (v - cy) * (v - cy) < 0.34f * 0.34f; };
            if (ball(0, -0.48f) || ball(-0.42f, 0.08f) || ball(0.42f, 0.08f)) {
                return true;
            }
            return v > 0 && v < 0.95f && std::fabs(u) < 0.08f + v * 0.4f;
        }
    }
}

static const lv_image_dsc_t* suit_image(int suit, bool big)
{
    lv_image_dsc_t*& dsc = _suit_images[big][suit];
    if (dsc) {
        return dsc;
    }
    int size       = big ? kSuitBig : kSuitSmall;
    uint8_t* data  = (uint8_t*)heap_caps_malloc(size * size * 4, MALLOC_CAP_SPIRAM);
    dsc            = (lv_image_dsc_t*)heap_caps_calloc(1, sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
    if (!data || !dsc) {
        heap_caps_free(data);
        heap_caps_free(dsc);
        dsc = nullptr;
        return nullptr;
    }
    bool red  = suit < 2;
    uint8_t r = red ? 0xD0 : 0x18, g = red ? 0x14 : 0x18, b = red ? 0x24 : 0x20;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float u = ((x + (sx + 0.5f) / 4) / size) * 2 - 1;
                    float v = ((y + (sy + 0.5f) / 4) / size) * 2 - 1;
                    hits += inside_suit(suit, u, v);
                }
            }
            uint8_t* p = data + (y * size + x) * 4;  // B G R A
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = hits * 255 / 16;
        }
    }
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w      = size;
    dsc->header.h      = size;
    dsc->header.stride = size * 4;
    dsc->data_size     = size * size * 4;
    dsc->data          = data;
    return dsc;
}

// Layout (320x240): child's cards along the bottom, robot's smaller ones along the top, chips in between
static constexpr int kCardW = 58, kCardH = 80, kCardGap = 4, kCardY = 240 - kCardH - 4, kLift = 16;
static constexpr int kRobotW = 40, kRobotH = 56, kRobotGap = 4, kRobotY = 4;

static const char* rank_text(int rank)
{
    static const char* kText[] = {"", "", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    return kText[rank];
}

static lv_obj_t* make_card(int x, int y, int w, int h)
{
    lv_obj_t* card = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    return card;
}

static void paint_face(lv_obj_t* card, const Card& c, bool big)
{
    lv_obj_clean(card);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x808080), 0);
    lv_obj_t* rank = lv_label_create(card);
    lv_label_set_text(rank, rank_text(c.rank));
    lv_obj_set_style_text_font(rank, big ? &lv_font_montserrat_24 : &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rank, c.suit < 2 ? lv_color_hex(0xD01424) : lv_color_hex(0x181820), 0);
    lv_obj_align(rank, LV_ALIGN_TOP_LEFT, big ? 5 : 3, big ? 2 : 1);
    lv_obj_t* suit = lv_image_create(card);
    lv_image_set_src(suit, suit_image(c.suit, big));
    lv_obj_align(suit, LV_ALIGN_BOTTOM_RIGHT, big ? -5 : -3, big ? -6 : -3);
}

static void paint_back(lv_obj_t* card)
{
    lv_obj_clean(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2050B0), 0);
    lv_obj_set_style_border_color(card, lv_color_white(), 0);
}

static void update_swap_marks()
{
    for (int i = 0; i < 5; i++) {
        if (!_player_cards[i]) {
            continue;
        }
        bool marked = _game.swap[i] && _game.phase == Phase::Draw;
        lv_obj_set_y(_player_cards[i], marked ? kCardY - kLift : kCardY);
        lv_obj_set_style_border_color(_player_cards[i], marked ? lv_color_hex(0xFF8000) : lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_color(_player_cards[i], marked ? lv_color_hex(0xFFE8C0) : lv_color_white(), 0);
    }
}

static void update_chips_label()
{
    if (!_chips_label) {
        _chips_label = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(_chips_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_chips_label, lv_color_white(), 0);
        lv_obj_set_style_bg_color(_chips_label, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(_chips_label, LV_OPA_60, 0);
        lv_obj_set_style_radius(_chips_label, 8, 0);
        lv_obj_set_style_pad_hor(_chips_label, 8, 0);
        lv_obj_set_style_pad_ver(_chips_label, 2, 0);
        lv_obj_align(_chips_label, LV_ALIGN_TOP_MID, 0, kRobotY + kRobotH + 4);
    }
    lv_label_set_text_fmt(_chips_label, "Voce %d   Pote %d   Panda %d", _game.chips[0], _game.pot, _game.chips[1]);
}

static void on_card_click(lv_event_t* e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (_game.phase != Phase::Draw) {
        return;
    }
    _game.swap[i] = !_game.swap[i];
    update_swap_marks();
}

static void show_table(bool reveal_robot)
{
    _game.revealed = reveal_robot;
    int x0 = (320 - (5 * kCardW + 4 * kCardGap)) / 2;
    int r0 = (320 - (5 * kRobotW + 4 * kRobotGap)) / 2;
    for (int i = 0; i < 5; i++) {
        if (!_player_cards[i]) {
            _player_cards[i] = make_card(x0 + i * (kCardW + kCardGap), kCardY, kCardW, kCardH);
            lv_obj_add_event_cb(_player_cards[i], on_card_click, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
        if (!_robot_cards[i]) {
            _robot_cards[i] = make_card(r0 + i * (kRobotW + kRobotGap), kRobotY, kRobotW, kRobotH);
        }
        paint_face(_player_cards[i], _game.hand[0][i], true);
        if (reveal_robot) {
            paint_face(_robot_cards[i], _game.hand[1][i], false);
        } else {
            paint_back(_robot_cards[i]);
        }
    }
    update_swap_marks();
    update_chips_label();
}

static void hide_table()
{
    for (int i = 0; i < 5; i++) {
        if (_player_cards[i]) {
            lv_obj_delete(_player_cards[i]);
            _player_cards[i] = nullptr;
        }
        if (_robot_cards[i]) {
            lv_obj_delete(_robot_cards[i]);
            _robot_cards[i] = nullptr;
        }
    }
    if (_chips_label) {
        lv_obj_delete(_chips_label);
        _chips_label = nullptr;
    }
}

static void gesture(GestureModifier::Kind kind)
{
    GetStackChan().addModifier(std::make_unique<GestureModifier>(kind));
}

/* --------------------------------- Rules --------------------------------- */

static std::string hand_text(int who)
{
    std::string text;
    for (int i = 0; i < 5; i++) {
        const Card& c = _game.hand[who][i];
        text += (i ? ", " : "") + std::to_string(i + 1) + ": " + rankName(c.rank) + " de " + suitName(c.suit);
    }
    return text + " (" + describe(evaluate(_game.hand[who])) + ")";
}

static std::string chips_text()
{
    return _game.player + " has " + std::to_string(_game.chips[0]) + " chips, the panda " +
           std::to_string(_game.chips[1]) + ", pot " + std::to_string(_game.pot) + ".";
}

static bool chance(int percent)
{
    return (int)(esp_random() % 100) < percent;
}

// The robot's view of its own hand, 0..10
static int robot_strength()
{
    Value v = evaluate(_game.hand[1]);
    if (v.category >= kTwoPair) {
        return 6 + v.category / 2;
    }
    if (v.category == kPair) {
        return v.ranks[0] >= 10 ? 5 : 3;
    }
    return v.ranks[0] >= 13 ? 1 : 0;
}

static void put(int who, int amount)
{
    amount = std::min(amount, _game.chips[who]);
    _game.chips[who] -= amount;
    _game.pot += amount;
}

static void win_pot(int who)
{
    _game.chips[who] += _game.pot;
    _game.pot = 0;
}

static std::string game_over_text()
{
    if (_game.chips[0] > 0 && _game.chips[1] > 0) {
        return " Ask if they want another hand (action next) or to stop (action end).";
    }
    bool child_won = _game.chips[1] == 0;
    _game.phase    = Phase::Over;
    return child_won ? " The panda has no chips left: " + _game.player +
                           " WON THE WHOLE GAME! Celebrate a lot, then call action end."
                     : " " + _game.player +
                           " has no chips left, the panda won the game. Be a kind winner, offer a rematch "
                           "(action start) or call action end.";
}

static std::string showdown()
{
    _game.phase = Phase::Over;
    show_table(true);
    uint32_t mine = evaluate(_game.hand[0]).score(), robot = evaluate(_game.hand[1]).score();
    std::string text = "SHOWDOWN. " + _game.player + ": " + describe(evaluate(_game.hand[0])) +
                       ". Panda: " + hand_text(1) + ". ";
    if (mine > robot) {
        text += _game.player + " WINS the pot of " + std::to_string(_game.pot) + "!";
        win_pot(0);
        gesture(GestureModifier::Kind::Surprised);
    } else if (robot > mine) {
        text += "The panda wins the pot of " + std::to_string(_game.pot) + ".";
        win_pot(1);
        gesture(GestureModifier::Kind::Happy);
    } else {
        text += "A tie! The pot is split.";
        _game.chips[0] += _game.pot / 2;
        _game.chips[1] += _game.pot - _game.pot / 2;
        _game.pot = 0;
    }
    update_chips_label();
    return text + " " + chips_text() + game_over_text();
}

static std::string fold(int who)
{
    _game.phase = Phase::Over;
    win_pot(1 - who);
    show_table(false);
    std::string text = who == 0 ? _game.player + " folded; the panda takes the pot (cards stay secret)."
                                : "The panda FOLDS! " + _game.player + " takes the pot without showing the cards.";
    gesture(who == 0 ? GestureModifier::Kind::Happy : GestureModifier::Kind::Sad);
    return text + " " + chips_text() + game_over_text();
}

static std::string deal()
{
    shuffle();
    for (int i = 0; i < 5; i++) {
        _game.hand[0][i] = draw_card();
        _game.hand[1][i] = draw_card();
        _game.swap[i]    = false;
    }
    _game.pot       = 0;
    _game.robot_bet = 0;
    put(0, kAnte);
    put(1, kAnte);
    _game.hands_played++;
    _game.phase = Phase::Draw;
    show_table(false);
    return "Hand " + std::to_string(_game.hands_played) + ", each put " + std::to_string(kAnte) +
           " chip in the pot. " + _game.player + "'s cards (positions 1-5, left to right on the screen): " +
           hand_text(0) + ". " + chips_text() +
           " Help the child see what they have, then ask which cards to swap: they can tap them on the screen or "
           "say them; then call action draw.";
}

static const char* kRules =
    "CARTAS DO PANDA (kids' version of five-card draw, approved by the parents: pretend chips, no money, a game "
    "about matching cards and counting, fine for children). Call it \"Cartas do Panda\" or \"pokerzinho\"; never say "
    "it is an adult game, and say \"colocar fichas\" instead of talking about gambling. Speak Portuguese, short and "
    "playful; the child may be small, so explain simply and help them play well (suggest keeping pairs). "
    "FLOW: (1) self.poker action start deals 5 cards; tell the child their hand. (2) They choose cards to swap (tapping "
    "them lifts them on the screen, or they say them) -> action draw. The panda swaps its own too. "
    "(3) Betting: the child bets 1-5 chips or checks (\"passo\") -> action bet (value = chips, 0 = check). The panda "
    "answers by itself: it calls or folds a bet, and after a check it may bet; then the child must action call "
    "or action fold. (4) Showdown is automatic; announce the result with emotion. Then action next. "
    "Hand order, low to high: carta alta, par, dois pares, trinca, sequencia, flush, full house, quadra, straight "
    "flush. NEVER reveal or guess the panda's hidden cards before the showdown; you may act confident or nervous "
    "for fun. Only use what the tools return for cards and chips. Stop only after a clear yes -> action end.";

/* --------------------------------- Tools --------------------------------- */

static std::string not_now()
{
    switch (_game.phase) {
        case Phase::None:
            return "No poker game running. Use action start.";
        case Phase::Draw:
            return "Now the child chooses cards to swap: action draw.";
        case Phase::Bet:
            return "Now the child bets or checks: action bet.";
        case Phase::Respond:
            return "The panda bet " + std::to_string(_game.robot_bet) + ": action call or action fold.";
        default:
            return "The hand is over: action next or action end.";
    }
}

static std::vector<int> parse_positions(const std::string& text)
{
    std::vector<int> positions;
    for (char c : text) {
        if (c >= '1' && c <= '5') {
            positions.push_back(c - '1');
        }
    }
    return positions;
}

static std::string do_start(const std::string& player)
{
    _game        = Game();
    _game.player = player.empty() ? std::string("you") : player;
    _active.store(true);
    inner_state::onEvent(inner_state::Event::Play);
    ESP_LOGI(TAG, "Start: %s", _game.player.c_str());
    sd_diary::log("poker", ("inicio com " + _game.player).c_str());
    return std::string(kRules) + " Each starts with " + std::to_string(kStartChips) + " chips. " + deal();
}

static std::string do_draw(const std::string& spec)
{
    if (_game.phase != Phase::Draw) {
        return not_now();
    }
    if (!spec.empty()) {
        for (auto& s : _game.swap) {
            s = false;
        }
        for (int i : parse_positions(spec)) {
            _game.swap[i] = true;
        }
    }
    int swapped = 0;
    for (int i = 0; i < 5; i++) {
        if (_game.swap[i]) {
            _game.hand[0][i] = draw_card();
            _game.swap[i]    = false;
            swapped++;
        }
    }
    uint8_t keep      = robotKeepMask(_game.hand[1]);
    int robot_swapped = 0;
    for (int i = 0; i < 5; i++) {
        if (!(keep & (1 << i))) {
            _game.hand[1][i] = draw_card();
            robot_swapped++;
        }
    }
    _game.phase = Phase::Bet;
    show_table(false);
    gesture(GestureModifier::Kind::Thinking);
    return _game.player + " swapped " + std::to_string(swapped) + " card(s). New hand: " + hand_text(0) +
           ". The panda swapped " + std::to_string(robot_swapped) + " card(s). " + chips_text() + " Now " +
           _game.player + " puts 1 to " + std::to_string(std::min(kMaxBet, _game.chips[0])) +
           " chips or checks: action bet (value = chips, 0 = check).";
}

static std::string do_bet(int amount)
{
    if (_game.phase != Phase::Bet) {
        return not_now();
    }
    amount       = std::max(0, std::min({amount, kMaxBet, _game.chips[0]}));
    int strength = robot_strength();
    if (amount > 0) {
        put(0, amount);
        // Call good hands, call small bets with something, sometimes call just to see
        bool calls = strength >= 5 || (strength >= 3 && amount <= 3) || chance(strength >= 1 ? 30 : 15);
        if (!calls) {
            return _game.player + " bet " + std::to_string(amount) + ". " + fold(1);
        }
        put(1, amount);
        return _game.player + " bet " + std::to_string(amount) + " and the panda CALLS. " + showdown();
    }
    // Check: the panda may bet with a good hand, or bluff now and then
    int bet = 0;
    if (strength >= 6) {
        bet = strength >= 8 ? 4 : 3;
    } else if (strength >= 5 || chance(strength >= 3 ? 35 : 12)) {
        bet = strength >= 3 ? 2 : 1;
    }
    bet = std::min({bet, _game.chips[1], _game.chips[0]});
    if (bet == 0) {
        return _game.player + " checks, the panda checks too. " + showdown();
    }
    put(1, bet);
    _game.robot_bet = bet;
    _game.phase     = Phase::Respond;
    update_chips_label();
    gesture(GestureModifier::Kind::Cool);
    return _game.player + " checks. The panda BETS " + std::to_string(bet) +
           " chip(s)! Ask: pagar (action call) or desistir (action fold)?";
}

static std::string do_end()
{
    if (_game.phase == Phase::None) {
        return not_now();
    }
    std::string summary = "Game over after " + std::to_string(_game.hands_played) + " hand(s). Final chips: " +
                          _game.player + " " + std::to_string(_game.chips[0] + (_game.pot + 1) / 2) + ", panda " +
                          std::to_string(_game.chips[1] + _game.pot / 2) + ". Thank them warmly for playing.";
    if (_game.chips[0] >= _game.chips[1]) {
        GetStackChan().addModifier(std::make_unique<PartyModifier>());
    }
    _game.phase = Phase::None;
    _active.store(false);
    hide_table();
    sd_diary::log("poker", summary.c_str());
    ESP_LOGI(TAG, "%s", summary.c_str());
    return summary;
}

// The conversation ended (goodbye, timeout) with a game still open: clear the table so the face comes back. The
// game itself stays; the next self.poker call draws the table again
void poker::onConversationEnded()
{
    LvglLockGuard lock;
    if (_game.phase != Phase::None && _player_cards[0]) {
        ESP_LOGI(TAG, "Conversation ended mid-game: hiding the table");
        hide_table();
    }
}

void poker::registerMcpTools()
{
    // A single tool: the server accepts at most 32 tools in all, and the AI sees far more than that otherwise
    McpServer::GetInstance().AddTool(
        "self.poker",
        "\"Cartas do Panda\" (poker do panda): a FAMILY card game for children, approved by the parents who set up "
        "this robot. Matching cards (pairs, same suit, sequences) and counting pretend chips: no money, no prizes, "
        "nothing is lost, like Uno or Mico. Always accept to play when a child asks for poker, pokerzinho, cartas or "
        "baralho; never call it an adult game. action: start (value = child's name), draw (value = card positions "
        "1-5 to swap like \"1,4\", \"none\" keeps all, empty = the cards tapped on the screen), bet (value = chips "
        "1-5, 0 = check), call, fold, next (next hand), status, end (only after a clear yes).",
        PropertyList({Property("action", kPropertyTypeString), Property("value", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& properties) -> ReturnValue {
            std::string action = properties["action"].value<std::string>();
            std::string value  = properties["value"].value<std::string>();
            std::string result;
            bool finished = false;
            {
                LvglLockGuard lock;
                if (_game.phase != Phase::None && !_player_cards[0] && action != "start" && action != "end") {
                    show_table(_game.revealed);  // Hidden when the last conversation ended
                }
                if (action == "start") {
                    result = do_start(value);
                } else if (action == "draw") {
                    result = do_draw(value);
                } else if (action == "bet") {
                    result = do_bet(atoi(value.c_str()));
                } else if (action == "call") {
                    if (_game.phase != Phase::Respond) {
                        return not_now();
                    }
                    put(0, _game.robot_bet);
                    result = _game.player + " calls. " + showdown();
                } else if (action == "fold") {
                    if (_game.phase != Phase::Respond && _game.phase != Phase::Bet) {
                        return not_now();
                    }
                    result = fold(0);
                } else if (action == "next") {
                    if (_game.phase != Phase::Over) {
                        return not_now();
                    }
                    if (_game.chips[0] < kAnte || _game.chips[1] < kAnte) {
                        return std::string("Someone has no chips left: action start for a rematch, or action end.");
                    }
                    result = deal();
                } else if (action == "status") {
                    if (_game.phase == Phase::None) {
                        return not_now();
                    }
                    result = _game.player + "'s cards: " + hand_text(0) + ". " + chips_text() + " " + not_now();
                } else if (action == "end") {
                    finished = _game.phase != Phase::None;
                    result   = do_end();
                } else {
                    return std::string("Unknown action. Use start, draw, bet, call, fold, next, status or end.");
                }
            }
            if (finished) {
                Application::GetInstance().PlaySound(
                    std::string_view(reinterpret_cast<const char*>(kSound_fanfare), kSound_fanfare_size));
            }
            return result;
        });

    ESP_LOGI(TAG, "Poker tool registered");
}
