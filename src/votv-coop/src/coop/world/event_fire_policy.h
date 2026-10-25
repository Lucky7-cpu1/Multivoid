// coop/world/event_fire_policy.h -- the replay policy's one entry, private to event_fire_sync (a
// co-located src header, not include/): the rows themselves are event_fire_policy.cpp.
#pragma once

#include <string>

namespace coop::event_fire_sync {

// 1 replay, 0 known no-replay (laneOut names who carries the outputs, or why nobody does), -1
// unknown -- the default is no replay.
int ReplayVerdict(const std::string& name, const char** laneOut);

// The replayed rows a host may fire again in one session: the trigger-box scare arms. Re-arming one is the
// whole effect, so a client replays every fire of it rather than the first alone, and its own history of
// the row (the passEvents a joiner's save carried) does not stop the re-arm. The fear director re-arms
// these (coop/fear).
bool Rearmable(const std::string& name);

}  // namespace coop::event_fire_sync
