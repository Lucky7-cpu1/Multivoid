// ue_wrap/world/ambient_verbs.h -- the game's own ambient timer verbs, called on the live instance.
//
// The day-night cycle and the gamemode roll their ambient content from timers: a fog bank
// (daynightCycle_C::spawnFog), an insomniac spawn attempt (mainGamemode_C::trySpawnInsomniac). Each
// timer's success arm is one parameterless Blueprint function; calling that function is the same
// thing the timer does, with the game's own gates inside it (trySpawnInsomniac can still decline).
// Principle-7 wrapper: no network or gameplay state; whoever calls decides who may. Game thread.

#pragma once

namespace ue_wrap::ambient_verbs {

// Call the parameterless function `fn` on the live mainGamemode_C, its frame zeroed. False with no
// gamemode, a name its class chain does not declare, or a failed dispatch.
bool CallOnGamemode(const wchar_t* fn);

// The same on the live daynightCycle_C.
bool CallOnCycle(const wchar_t* fn);

}  // namespace ue_wrap::ambient_verbs
