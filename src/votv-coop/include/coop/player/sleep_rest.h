// coop/player/sleep_rest.h -- a player's rest at the game's native sleep rate, with the world's clock
// left at 1, and the shared night once everyone is in bed. Game thread throughout.

// The game's sleep is one engine call, SetGlobalTimeDilation(20), and every need it restores rides the
// dilated tick delta. A player in bed while another is still up cannot run the session's world at 20 (the
// host's world is what the others live in), so a script_gate POST watch on mainGamemode_C::sleep takes the
// dilation back to 1 in the frame of the call and, for the rest's length, gives the actors that carry its
// needs the same factor as their own CustomTimeDilation: the player's body (sleep, health, irradiation) and
// each effect actor of the gamemode's list (the vaccine's drain, the sleepy effect). Food, whose native fall
// divides the delta by the GLOBAL dilation, is restored by (1 - 1/F) of each fall; coffee, which drains in
// the gamemode's tick, loses (F - 1) times each fall more. The rest ends when the sleeping flag falls, with
// the session, or when the gamemode or the body changes. Timer-driven owners (the ariral pillow's rolls, the
// nightmare) are scaled by no dilation at 1 and are not touched here.
//
// The shared night is the host's: when the host lies down and every other player already rests in a bed
// (coop/player/sleep_sync's EveryoneElseResting), or the last of them lies down while the host rests, the
// host's world runs the game's own speed-up and every client's clock follows it. When anyone gets up, or a
// player joins, the host's world goes back to 1 and the host rests on like anyone else. A host with no
// other player sleeps the single-player way.

#pragma once

// Divergence from MTA, which syncs one game speed to every client (CMapManager.cpp:463): the session's speed
// follows the beds. An awake player at x20 is unplayable, so the night speeds up only when nobody is awake.

namespace coop::net { class Session; }

namespace coop::sleep_rest {

// Cache the session. Called from the subsystems Install fanout.
void Install(coop::net::Session* session);

// Per pump tick: register the sleep watch until it is live; on the host, move between its own rest and the
// shared night; while a rest runs, hold the factor on the effects and settle food and coffee. Cheap when
// idle: no engine read at all.
void Tick();

// The session ended: a rest in progress is handed back at factor 1, or, for a host still asleep, to the
// game's own speed-up.
void OnDisconnect();

}  // namespace coop::sleep_rest
