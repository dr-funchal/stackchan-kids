#include <hal/utils/poker_rules.h>
#include <cstdio>

using namespace poker_rules;

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static Hand h(int r0, int s0, int r1, int s1, int r2, int s2, int r3, int s3, int r4, int s4)
{
    return Hand{Card{(uint8_t)r0, (uint8_t)s0}, Card{(uint8_t)r1, (uint8_t)s1}, Card{(uint8_t)r2, (uint8_t)s2},
                Card{(uint8_t)r3, (uint8_t)s3}, Card{(uint8_t)r4, (uint8_t)s4}};
}

int main()
{
    CHECK(evaluate(h(2, 0, 5, 1, 9, 2, 11, 3, 13, 0)).category == kHighCard);
    CHECK(evaluate(h(9, 0, 9, 1, 2, 2, 11, 3, 13, 0)).category == kPair);
    CHECK(evaluate(h(9, 0, 9, 1, 2, 2, 2, 3, 13, 0)).category == kTwoPair);
    CHECK(evaluate(h(9, 0, 9, 1, 9, 2, 2, 3, 13, 0)).category == kTrips);
    CHECK(evaluate(h(5, 0, 6, 1, 7, 2, 8, 3, 9, 0)).category == kStraight);
    CHECK(evaluate(h(14, 0, 2, 1, 3, 2, 4, 3, 5, 0)).category == kStraight);
    CHECK(evaluate(h(2, 1, 6, 1, 9, 1, 11, 1, 13, 1)).category == kFlush);
    CHECK(evaluate(h(9, 0, 9, 1, 9, 2, 2, 3, 2, 0)).category == kFullHouse);
    CHECK(evaluate(h(9, 0, 9, 1, 9, 2, 9, 3, 2, 0)).category == kQuads);
    CHECK(evaluate(h(10, 2, 11, 2, 12, 2, 13, 2, 14, 2)).category == kStraightFlush);

    // Tie-breaks
    auto pair_k  = evaluate(h(13, 0, 13, 1, 2, 2, 3, 3, 4, 0));
    auto pair_q  = evaluate(h(12, 0, 12, 1, 14, 2, 10, 3, 9, 0));
    CHECK(pair_k.score() > pair_q.score());
    auto kick_hi = evaluate(h(8, 0, 8, 1, 14, 2, 3, 3, 2, 0));
    auto kick_lo = evaluate(h(8, 2, 8, 3, 13, 2, 12, 3, 11, 0));
    CHECK(kick_hi.score() > kick_lo.score());
    auto wheel = evaluate(h(14, 0, 2, 1, 3, 2, 4, 3, 5, 0));
    auto six   = evaluate(h(2, 0, 3, 1, 4, 2, 5, 3, 6, 0));
    CHECK(six.score() > wheel.score());
    CHECK(evaluate(h(2, 1, 6, 1, 9, 1, 11, 1, 13, 1)).score() > six.score());

    // Robot draw
    CHECK(robotKeepMask(h(9, 0, 9, 1, 2, 2, 11, 3, 13, 0)) == 0x03);
    CHECK(robotKeepMask(h(2, 1, 6, 1, 9, 1, 11, 1, 13, 0)) == 0x0F);
    CHECK(robotKeepMask(h(5, 0, 6, 1, 7, 2, 8, 3, 9, 0)) == 0x1F);
    CHECK(robotKeepMask(h(2, 0, 14, 1, 7, 2, 13, 3, 4, 0)) == 0x0A);
    CHECK(robotKeepMask(h(2, 0, 14, 1, 7, 2, 6, 3, 4, 0)) == 0x02);

    CHECK(describe(pair_k) == "um par de Rei");

    std::printf(failures ? "%d failures\n" : "all poker tests passed\n", failures);
    return failures ? 1 : 0;
}
