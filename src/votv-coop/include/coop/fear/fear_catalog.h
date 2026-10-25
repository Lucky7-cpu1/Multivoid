// coop/fear/fear_catalog.h -- the scares the fear director may fire, and how each reaches every
// player.
//
// The director picks only from this table, and a row is here only when an existing lane already
// carries its outcome to every peer: a creature the npc lane mirrors, a device state the device lanes
// carry, a fog actor the weather lane mirrors, or an eventer row the client replays itself
// (coop/world/event_fire_policy.cpp). A scare that only the host would see -- the alien noises, the
// vent knocker, the rock throwers -- is left out on purpose: two players looking at one window must
// both see what is behind it. `lane` names the carrier, so a row can be checked against the code.
//
// Trap rows arm a trigger the game placed in the level. The arm is replayed on every peer, so each
// player who walks into it gets the scare on their own machine (the game's scare is per viewer by
// design); a client replays a re-armed trap again (event_fire_policy's re-arm list).

#pragma once

#include <cstddef>
#include <cstdint>

namespace coop::fear {

// How a row is fired on the host. The two eventer verbs go through the shared fire seam
// (coop/world/event_fire_sync's HostFire), which broadcasts them; the two ambient verbs are the
// timer verbs the game itself calls on the gamemode or the day-night cycle (ue_wrap/world/ambient_verbs).
enum class FireVerb : uint8_t { RunEvent, SpecialEvent, GamemodeVerb, CycleVerb };

// What kind of fright a row is: the director's weights read it, the AI agent is told it.
enum class Flavor : uint8_t { Creature, Trap, Device, Atmosphere, Spectacle };

struct Scare {
    const char* key;      // the eventer row, or the ambient verb's function name
    FireVerb verb;
    Flavor flavor;
    uint8_t power;        // 1 unsettling, 2 a jolt, 3 something in the room
    bool nightOnly;       // fired only between 20:00 and 06:00 game time
    bool dangerous;       // can kill a player or wreck the base: only with director.allow_dangerous
    uint16_t cooldownS;   // real seconds before this row may fire again
    const char* lane;     // the lane that carries the outcome to every peer
    const char* what;     // one plain line: the log, the pane and the AI agent read it
};

// The whole table, in a fixed order (a row's index is stable for one build).
const Scare* Catalog(size_t* count);

// The row named `key`, or null.
const Scare* FindScare(const char* key);

// "creature", "trap", ... for the log, the pane and the AI agent's prompt.
const char* FlavorName(Flavor f);

}  // namespace coop::fear
