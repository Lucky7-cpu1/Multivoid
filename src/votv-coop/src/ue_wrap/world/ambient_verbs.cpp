// ue_wrap/world/ambient_verbs.cpp -- see ue_wrap/world/ambient_verbs.h.

#include "ue_wrap/world/ambient_verbs.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/world/daynightcycle.h"
#include "ue_wrap/world/world_singleton.h"

namespace ue_wrap::ambient_verbs {
namespace {

namespace R = ue_wrap::reflection;

// The function an instance of the object's class dispatches for `fn`, its own or an ancestor's. The
// frame is zeroed by ParamFrame: a Blueprint frame also holds the body's locals, which start zeroed in
// the game's own timer call as well.
bool CallOn(void* obj, const wchar_t* fn, const char* what) {
    if (!ue_wrap::game_thread::IsGameThread() || !obj || !fn || !R::IsLive(obj)) return false;
    void* declaring = nullptr;
    void* f = R::FindDispatchFunction(R::ClassOf(obj), fn, &declaring);
    if (!f) {
        UE_LOGW("ambient_verbs: %s has no function '%ls'", what, fn);
        return false;
    }
    ue_wrap::ParamFrame frame(f);
    if (!frame.valid()) return false;
    if (!ue_wrap::Call(obj, frame)) {
        UE_LOGW("ambient_verbs: %s::%ls dispatch failed", what, fn);
        return false;
    }
    UE_LOGI("ambient_verbs: %s::%ls dispatched", what, fn);
    return true;
}

}  // namespace

bool CallOnGamemode(const wchar_t* fn) {
    return CallOn(ue_wrap::world_singleton::Gamemode(), fn, "mainGamemode_C");
}

bool CallOnCycle(const wchar_t* fn) {
    if (!ue_wrap::daynightcycle::EnsureResolved()) return false;
    return CallOn(ue_wrap::daynightcycle::Cycle(), fn, "daynightCycle_C");
}

}  // namespace ue_wrap::ambient_verbs
