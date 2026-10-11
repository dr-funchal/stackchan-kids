#include <stackchan/inner_state/inner_state_core.h>
#include <cstdio>

using namespace inner_state;

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static void run(Model& m, float hours, bool napping, bool talking, uint8_t battery = 100)
{
    TickInput in;
    in.dt_ms   = 250;
    in.napping = napping;
    in.talking = talking;
    in.battery = battery;
    for (int i = 0; i < (int)(hours * 3600 * 4); i++) {
        m.tick(in);
    }
}

int main()
{
    // Alone and awake: misses company and starts searching for someone
    Model m;
    run(m, 1.5f, false, false);
    CHECK(m.drives().longing > kSearchAbove);
    CHECK(m.behavior().searching);
    CHECK(m.event(Event::ConversationStart));  // Someone came back: a reunion

    // A long night napping: rested, and longing for the family in the morning
    Model night;
    run(night, 8, true, false);
    CHECK(night.drives().energy == 100);
    CHECK(night.drives().longing > kReunionAbove);
    CHECK(night.event(Event::HeadPet));

    // Talking for a while: company satisfied, no reunion on the next touch
    Model chat;
    run(chat, 0.5f, false, true);
    CHECK(chat.drives().longing < 10);
    CHECK(!chat.event(Event::Touch));

    // Low energy wins over everything: sleepy face, slow moves
    Model tired;
    tired.set(Drives{10, 90, 90, 90});
    CHECK(tired.behavior().face == Face::Sleepy);
    CHECK(tired.behavior().drowsy);
    CHECK(tired.describe().find("sleepy") != std::string::npos);

    // Petting lifts the mood until it shows
    Model pet;
    pet.set(Drives{70, 20, 40, 60});
    pet.event(Event::HeadPet);
    pet.event(Event::HeadPet);
    CHECK(pet.behavior().face == Face::Happy);

    // Very lonely: sad face while searching
    Model lonely;
    lonely.set(Drives{70, 95, 40, 50});
    CHECK(lonely.behavior().face == Face::Sad && lonely.behavior().searching);

    // A low battery drains energy faster
    Model a, b;
    run(a, 1, false, false, 100);
    run(b, 1, false, false, 10);
    CHECK(b.drives().energy < a.drives().energy);

    // Values stay in range
    Model z;
    for (int i = 0; i < 50; i++) {
        z.event(Event::Shake);
    }
    CHECK(z.drives().mood == 0);

    if (failures == 0) {
        std::printf("inner_state_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
