// coop/fear/fear_director.h -- the fear director: the host decides when the night gets worse.
//
// The game schedules its scares by the calendar: a row of the event table fires at its hour on its
// day, the same for everyone, whatever the players are doing. The director adds a second clock that
// reads the players instead, the shape of Left 4 Dead's AI director: a tension value climbs while the
// night is quiet -- faster in the dark, faster while the players are apart -- and when it crosses a
// threshold the director fires one scare from the catalog (coop/fear/fear_catalog.h) and lets the
// players breathe for a while before it climbs again. The threshold is re-rolled after every scare, so
// the rhythm never settles into something a player can count.
//
// Authority: the host's alone, like every shared-world roll. A client runs none of this; what it sees
// arrives through the lane that carries each scare (the catalog names it). The director holds while a
// player is still joining, while anyone sleeps (director.respect_sleep), and while one of the game's
// own events is running, so it never stacks on a story beat.
//
// With director.ai on, an AI agent (coop/fear/fear_ai.h) looks at the same situation every
// director.ai_period_s seconds and chooses instead of the dice: which scare, whether to wait, and an
// occasional email to the base computer, which the email lane shows to everyone. The rules stay the
// safety net: the agent may only name a catalog row the rules would allow right now, and if it fails
// or keeps waiting at the top of the tension, the rules fire on their own.
//
// Game thread, except ReadView and the Request* calls, which any thread may make.

#pragma once

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::fear {

// Cache the session. The subsystems Install fanout.
void Install(coop::net::Session* session);

// The gameplay tick (subsystems TickGameplay). Does its work once a second; cheap otherwise.
void Tick();

// The session ended: the pacing and the history start over with the next one.
void OnDisconnect();

// What the F1 pane draws, copied under a lock.
struct View {
    bool hosting = false;      // this peer runs the director: it hosts the session
    bool enabled = false;      // director.enabled
    float tension = 0.f;       // 0..100
    float threshold = 0.f;     // the next scare's trigger point
    int relaxLeftS = 0;        // seconds of breather left after the last scare
    char hold[112] = {};       // why it is holding right now, empty when it is not
    bool aiOn = false;         // director.ai and a key in hand
    char aiState[112] = {};    // the agent's last outcome, for the host
    char aiNote[240] = {};     // the agent's own last sentence about its plan
    static constexpr int kLines = 8;
    int lineCount = 0;
    char lines[kLines][160] = {};  // the recent scares, newest first
};
void ReadView(View& out);

// The pane's buttons, from any thread; each runs on the game thread at the next pump and only on a
// hosting peer. Fire: one catalog row now, its cooldown and the holds ignored (a test of its lane).
// Pick: the director chooses one now, as at a peak. Calm: tension to zero and a fresh breather.
void RequestFire(const char* key);
void RequestPick();
void RequestCalm();

}  // namespace coop::fear
