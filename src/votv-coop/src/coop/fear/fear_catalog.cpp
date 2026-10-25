// coop/fear/fear_catalog.cpp -- see coop/fear/fear_catalog.h. Every `lane` below is the
// verdict coop/world/event_fire_policy.cpp or the spawner table in docs/npcs-and-kerfur.md gives the
// row; a row whose outcome no lane carries does not belong here.

#include "coop/fear/fear_catalog.h"

#include <cstring>

namespace coop::fear {
namespace {

using V = FireVerb;
using F = Flavor;

const Scare kScares[] = {
    // ---- creatures: a host-run brain, mirrored to every peer by the npc lane ----
    { "ventCrawler", V::RunEvent, F::Creature, 3, false, false, 1500, "npc lane (allowlisted crawler)",
      "something crawls through the base's air vents" },
    { "tentacleBalls", V::RunEvent, F::Creature, 3, false, false, 1800, "npc lane (follower catch)",
      "floating tentacle balls drift in and follow the players" },
    { "trySpawnInsomniac", V::GamemodeVerb, F::Creature, 3, true, false, 1200, "npc lane (host-spawned insomniac)",
      "the insomniac may come out of the dark (the game can still decline)" },
    { "graysforest", V::RunEvent, F::Creature, 3, false, true, 3600, "npc lane (controller catch)",
      "grays invade the forest around the base" },
    { "eggvasion", V::RunEvent, F::Creature, 3, false, true, 3600, "npc and world-actor lanes",
      "an egg invasion begins" },

    // ---- traps: a level trigger armed on every peer; each player gets it on their own walk-in ----
    { "toeStab", V::RunEvent, F::Trap, 2, false, false, 600, "eventer replay (re-armed on every peer)",
      "arms the toe-stab scare: the next player to walk into it feels it" },
    { "falseEnter", V::RunEvent, F::Trap, 2, false, false, 600, "eventer replay (re-armed on every peer)",
      "arms the false-enter scare: someone seems to come in" },
    { "mann", V::RunEvent, F::Trap, 2, false, false, 600, "eventer replay (re-armed on every peer)",
      "arms the mannequin scare" },
    { "vent", V::RunEvent, F::Trap, 2, false, false, 600, "eventer replay (re-armed on every peer)",
      "arms the vent scare" },
    { "fakeGrays", V::RunEvent, F::Trap, 2, false, false, 900, "eventer replay (re-armed on every peer)",
      "arms a gray hallucination" },

    // ---- the base's own devices turning on the players: the device lanes carry each state ----
    { "lightswitch", V::SpecialEvent, F::Device, 2, true, false, 600, "light switch and group lanes",
      "the base's lights turn themselves off" },
    { "console", V::SpecialEvent, F::Device, 1, false, false, 900, "terminal lanes",
      "a terminal starts typing commands on its own" },
    { "keypadGuess", V::SpecialEvent, F::Device, 1, false, false, 1200, "keypad lane",
      "someone outside tries codes on a door keypad" },

    // ---- atmosphere and spectacle ----
    { "spawnFog", V::CycleVerb, F::Atmosphere, 1, false, false, 1500, "weather fog lane (the host's fog actor)",
      "a thick fog rolls in" },
    { "solar", V::RunEvent, F::Spectacle, 2, false, false, 1800, "eventer replay (cosmetic, lights converge)",
      "a blinding flash and a boom; the lights go dark" },
};
constexpr size_t kScareCount = sizeof(kScares) / sizeof(kScares[0]);

}  // namespace

const Scare* Catalog(size_t* count) {
    if (count) *count = kScareCount;
    return kScares;
}

const Scare* FindScare(const char* key) {
    if (!key) return nullptr;
    for (const Scare& s : kScares)
        if (std::strcmp(s.key, key) == 0) return &s;
    return nullptr;
}

const char* FlavorName(Flavor f) {
    switch (f) {
        case Flavor::Creature:   return "creature";
        case Flavor::Trap:       return "trap";
        case Flavor::Device:     return "device";
        case Flavor::Atmosphere: return "atmosphere";
        case Flavor::Spectacle:  return "spectacle";
    }
    return "?";
}

}  // namespace coop::fear
