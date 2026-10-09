/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

/**
 * @brief Pure five-card draw rules (no hardware): hand ranking and the robot's draw choice.
 * Kept header-only so the host tests in tests/ can check them on the Mac
 */
namespace poker_rules {

// rank 2..14 (11 J, 12 Q, 13 K, 14 A); suit 0 copas, 1 ouros, 2 espadas, 3 paus
struct Card {
    uint8_t rank = 0;
    uint8_t suit = 0;
};

using Hand = std::array<Card, 5>;

enum Category {
    kHighCard = 0,
    kPair,
    kTwoPair,
    kTrips,
    kStraight,
    kFlush,
    kFullHouse,
    kQuads,
    kStraightFlush,
};

struct Value {
    int category = kHighCard;
    std::array<int, 5> ranks{};  // Tie-break order: grouped ranks first (the pair before the kickers)

    // One comparable number: category, then the five ranks, 4 bits each
    uint32_t score() const
    {
        uint32_t s = category;
        for (int r : ranks) {
            s = (s << 4) | (uint32_t)r;
        }
        return s;
    }
};

inline Value evaluate(const Hand& hand)
{
    int count[15] = {};
    for (auto& c : hand) {
        count[c.rank]++;
    }
    bool flush = std::all_of(hand.begin(), hand.end(), [&](const Card& c) { return c.suit == hand[0].suit; });

    // Ranks ordered by (count, rank), highest first: {K,K,7,7,2} -> K K 7 7 2
    std::array<int, 5> ranks{};
    for (int i = 0; i < 5; i++) {
        ranks[i] = hand[i].rank;
    }
    std::sort(ranks.begin(), ranks.end(), [&](int a, int b) {
        return count[a] != count[b] ? count[a] > count[b] : a > b;
    });

    int distinct = 0;
    for (int r = 2; r <= 14; r++) {
        distinct += count[r] > 0;
    }
    bool straight = false;
    if (distinct == 5) {
        if (ranks[0] - ranks[4] == 4) {
            straight = true;
        } else if (ranks[0] == 14 && ranks[1] == 5) {  // A-2-3-4-5: the ace plays low
            straight = true;
            ranks    = {5, 4, 3, 2, 1};
        }
    }

    Value v;
    v.ranks = ranks;
    int top = count[ranks[0] == 1 ? 14 : ranks[0]];
    if (straight && flush) {
        v.category = kStraightFlush;
    } else if (top == 4) {
        v.category = kQuads;
    } else if (top == 3 && count[ranks[3]] == 2) {
        v.category = kFullHouse;
    } else if (flush) {
        v.category = kFlush;
    } else if (straight) {
        v.category = kStraight;
    } else if (top == 3) {
        v.category = kTrips;
    } else if (top == 2 && count[ranks[2]] == 2) {
        v.category = kTwoPair;
    } else if (top == 2) {
        v.category = kPair;
    }
    return v;
}

// Which cards the robot keeps before the draw (bit i = keep card i). Simple, sensible play:
// keep made hands, the pairs/trips, four to a flush, else the high cards
inline uint8_t robotKeepMask(const Hand& hand)
{
    Value v = evaluate(hand);
    if (v.category == kStraight || v.category == kFlush || v.category >= kFullHouse) {
        return 0x1F;
    }
    int count[15] = {};
    for (auto& c : hand) {
        count[c.rank]++;
    }
    uint8_t mask = 0;
    if (v.category != kHighCard) {
        for (int i = 0; i < 5; i++) {
            if (count[hand[i].rank] >= 2) {
                mask |= 1 << i;
            }
        }
        return mask;
    }
    for (int s = 0; s < 4; s++) {
        uint8_t suited = 0;
        int n          = 0;
        for (int i = 0; i < 5; i++) {
            if (hand[i].suit == s) {
                suited |= 1 << i;
                n++;
            }
        }
        if (n == 4) {
            return suited;
        }
    }
    // Highest card, plus the second if both are 10 or more
    int best = 0, second = -1;
    for (int i = 1; i < 5; i++) {
        if (hand[i].rank > hand[best].rank) {
            second = best;
            best   = i;
        } else if (second < 0 || hand[i].rank > hand[second].rank) {
            second = i;
        }
    }
    mask = 1 << best;
    if (hand[best].rank >= 10 && hand[second].rank >= 10) {
        mask |= 1 << second;
    }
    return mask;
}

inline const char* rankName(int rank)
{
    static const char* kNames[] = {"?", "As", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Valete", "Dama", "Rei", "As"};
    return rank >= 1 && rank <= 14 ? kNames[rank] : "?";
}

inline const char* suitName(int suit)
{
    static const char* kNames[] = {"copas", "ouros", "espadas", "paus"};
    return suit >= 0 && suit < 4 ? kNames[suit] : "?";
}

inline const char* categoryName(int category)
{
    static const char* kNames[] = {"carta alta",  "um par", "dois pares", "trinca",     "sequencia",
                                   "flush (5 do mesmo naipe)", "full house", "quadra", "straight flush"};
    return category >= 0 && category <= kStraightFlush ? kNames[category] : "?";
}

// "par de Rei", "trinca de 7", "sequencia ate o 9"...
inline std::string describe(const Value& v)
{
    std::string text = categoryName(v.category);
    switch (v.category) {
        case kHighCard:
            return text + " " + rankName(v.ranks[0]);
        case kPair:
        case kTrips:
        case kQuads:
            return text + " de " + rankName(v.ranks[0]);
        case kTwoPair:
            return text + " (" + rankName(v.ranks[0]) + " e " + rankName(v.ranks[2]) + ")";
        case kFullHouse:
            return text + " (" + rankName(v.ranks[0]) + " com " + rankName(v.ranks[3]) + ")";
        case kStraight:
        case kStraightFlush:
            return text + " ate o " + rankName(v.ranks[0]);
        default:
            return text;
    }
}

}  // namespace poker_rules
