// coop/fear/fear_director.cpp -- see coop/fear/fear_director.h.

#include "coop/fear/fear_director.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/fear/fear_ai.h"
#include "coop/fear/fear_catalog.h"
#include "coop/net/net_clock.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/player/roster_ledger.h"
#include "coop/player/sleep_sync.h"
#include "coop/text/utf8_codec.h"
#include "coop/world/event_fire_sync.h"

#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/world_identity.h"
#include "ue_wrap/world/active_events.h"
#include "ue_wrap/world/ambient_verbs.h"
#include "ue_wrap/world/daynightcycle.h"
#include "ue_wrap/world/email.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <string>

namespace coop::fear {
namespace {

namespace CR = coop::config_registry;
namespace CFG = coop::config;
namespace GT = ue_wrap::game_thread;
using json = nlohmann::json;

std::atomic<coop::net::Session*> g_session{nullptr};

// ---- the pacing constants --------------------------------------------------------------------------
constexpr uint64_t kTickMs = 1000;
// Tension gained a second at intensity 1, 2 and 3: from zero to a threshold of 70 that is about 19, 12
// and 7 minutes in daylight, and roughly half that in the dark.
constexpr float kGain[4] = {0.f, 0.06f, 0.10f, 0.17f};
constexpr float kNightMul = 1.8f;    // 20:00..05:59
constexpr float kDuskMul = 1.25f;    // 18:00..19:59
constexpr float kApartMul = 1.4f;    // the players are split up
constexpr float kRestMul = 0.3f;     // someone sleeps and the director respects it
constexpr float kRelaxGainMul = 0.25f;
constexpr float kApartM = 40.f;      // two players this far apart are apart; a player this far from all is alone
constexpr float kTogetherM = 15.f;
// The breather after a scare, scaled by intensity (index 1..3).
constexpr float kRelaxMul[4] = {1.f, 1.6f, 1.f, 0.6f};
// A peak waits this long for the agent's choice before the rules fire on their own: long enough for the
// agent to hold out for a better moment (a player wandering off), short enough that the night never stalls.
constexpr uint64_t kAiPeakWaitMs = 90000;
// The agent may not fire below this tension, whatever it says: it chooses moments, it does not spam.
constexpr float kAiMinTension = 35.f;
constexpr int kAiFailLimit = 3;
constexpr uint64_t kAiBackoffMs = 5ull * 60 * 1000;
constexpr uint64_t kEmailGapMs = 10ull * 60 * 1000;
// The email sender the agent writes as: enum_emailChars' third entry, the base's automatic system.
constexpr uint8_t kEmailSender = 2;
constexpr size_t kMaxRows = 32;
constexpr size_t kFiredKept = View::kLines;

struct Fired {
    uint64_t atMs = 0;
    const Scare* scare = nullptr;
    const char* by = "";  // "rules", "agent", "test"
};

// The director's whole state. Game thread.
struct State {
    float tension = 0.f;
    float threshold = 70.f;
    uint64_t lastTickMs = 0;
    uint64_t relaxUntilMs = 0;
    uint64_t lastFireMs[kMaxRows] = {};
    uint64_t lastEmailMs = 0;
    uint64_t lastAskMs = 0;
    uint64_t peakSinceMs = 0;
    int aiFails = 0;
    uint64_t aiBackoffUntilMs = 0;
    int aiWaitsAtPeak = 0;
    std::deque<Fired> fired;  // newest first
    std::mt19937 rng{0x5EEDu};
    bool seeded = false;
    std::string aiState;
    std::string aiNote;
    std::string hold;
};
State g;

std::mutex g_viewMu;
View g_view;

// ---- what the director sees each second ------------------------------------------------------------
struct Seen {
    int slot = 0;
    bool alive = false;
    ue_wrap::FVector pos{};
    float nearestM = -1.f;  // to the nearest other living player; -1 with nobody else
    coop::net::SleepPeerState rest = coop::net::SleepPeerState::Awake;
};

struct Obs {
    bool gameplay = false;
    int hour = 12, minute = 0, day = 0;
    bool night = false, dusk = false;
    bool joining = false;
    bool anyResting = false;
    int activeEvents = 0;
    float apartM = 0.f;  // the largest distance between two living players
    Seen p[coop::players::kMaxPeers];
    int n = 0;
};

float DistM(const ue_wrap::FVector& a, const ue_wrap::FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) / 100.f;  // centimetres to metres
}

Obs Observe(coop::net::Session& s) {
    Obs o;
    o.gameplay = ue_wrap::world_identity::CurrentWorldKind() == ue_wrap::world_identity::WorldKind::Gameplay;
    if (!o.gameplay) return o;
    int32_t h = 0, m = 0, d = 0;
    if (ue_wrap::daynightcycle::ReadTimeZ(h, m, d)) { o.hour = h; o.minute = m; o.day = d; }
    o.night = o.hour >= 20 || o.hour < 6;
    o.dusk = o.hour >= 18 && o.hour < 20;
    int32_t events = 0;
    if (ue_wrap::active_events::EnsureResolved() && ue_wrap::active_events::ReadCount(events)) o.activeEvents = events;

    auto& reg = coop::players::Registry::Get();
    for (int slot = 0; slot < static_cast<int>(coop::players::kMaxPeers); ++slot) {
        if (slot > 0 && !coop::roster_ledger::Occupied(slot)) continue;
        if (slot > 0 && !s.IsSlotWorldReady(slot)) { o.joining = true; continue; }
        Seen& p = o.p[o.n++];
        p.slot = slot;
        p.rest = coop::sleep_sync::PeerState(slot);
        if (p.rest != coop::net::SleepPeerState::Awake) o.anyResting = true;
        void* body = nullptr;
        if (slot == 0) {
            body = reg.Local();
        } else if (reg.LifeOf(static_cast<uint8_t>(slot)) == coop::players::PuppetLife::Alive) {
            if (coop::RemotePlayer* rp = reg.Puppet(static_cast<uint8_t>(slot))) body = rp->GetActor();
        }
        p.alive = body && ue_wrap::reflection::IsLive(body) && ue_wrap::engine::TryGetActorLocation(body, p.pos);
    }
    for (int i = 0; i < o.n; ++i) {
        if (!o.p[i].alive) continue;
        for (int j = 0; j < o.n; ++j) {
            if (i == j || !o.p[j].alive) continue;
            const float dm = DistM(o.p[i].pos, o.p[j].pos);
            if (o.p[i].nearestM < 0.f || dm < o.p[i].nearestM) o.p[i].nearestM = dm;
            o.apartM = std::max(o.apartM, dm);
        }
    }
    return o;
}

// ---- the catalog's rows, as the rules allow them now ------------------------------------------------
size_t IndexOf(const Scare* sc) {
    size_t count = 0;
    const Scare* all = Catalog(&count);
    return static_cast<size_t>(sc - all);
}

int Intensity() { return static_cast<int>(std::clamp(CFG::ResolveInt(CR::rows::director_intensity), 1L, 3L)); }

bool CooledDown(const Scare& sc, uint64_t now) {
    const size_t i = IndexOf(&sc);
    if (i >= kMaxRows || g.lastFireMs[i] == 0) return true;
    return now - g.lastFireMs[i] >= static_cast<uint64_t>(sc.cooldownS) * 1000ull;
}

bool AllowedNow(const Scare& sc, const Obs& o, uint64_t now) {
    if (sc.dangerous && !CFG::ResolveFlag(CR::rows::director_allow_dangerous)) return false;
    if (sc.nightOnly && !o.night) return false;
    return CooledDown(sc, now);
}

const char* RestName(coop::net::SleepPeerState r, bool alive) {
    if (!alive) return "dead or not in the world";
    switch (r) {
        case coop::net::SleepPeerState::Resting:  return "asleep in bed";
        case coop::net::SleepPeerState::Dreaming: return "in a nightmare";
        default:                                  return "awake";
    }
}

std::string NameOf(int slot) { return coop::text::ToUtf8(coop::roster_ledger::DisplayName(slot)); }

// ---- firing --------------------------------------------------------------------------------------
bool Dispatch(const Scare& sc) {
    const std::wstring key(sc.key, sc.key + std::strlen(sc.key));
    namespace efs = coop::event_fire_sync;
    switch (sc.verb) {
        case FireVerb::RunEvent:     return efs::HostFire(efs::FireKind::RunEvent, key, L"None");
        case FireVerb::SpecialEvent: return efs::HostFire(efs::FireKind::SpecialEvent, key, L"None");
        case FireVerb::GamemodeVerb: return ue_wrap::ambient_verbs::CallOnGamemode(key.c_str());
        case FireVerb::CycleVerb:    return ue_wrap::ambient_verbs::CallOnCycle(key.c_str());
    }
    return false;
}

float Uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g.rng); }

// A scare went out: the tension drops by its power, the next threshold is rolled, the breather begins.
void Fire(const Scare& sc, const char* by, uint64_t now) {
    if (!Dispatch(sc)) {
        UE_LOGW("director: '%s' did not dispatch (%s)", sc.key, by);
        return;
    }
    const size_t i = IndexOf(&sc);
    if (i < kMaxRows) g.lastFireMs[i] = now;
    const float before = g.tension;
    if (std::strcmp(by, "test") != 0) {
        g.tension = std::max(0.f, g.tension - (20.f + 15.f * static_cast<float>(sc.power)));
        g.threshold = Uniform(55.f, 90.f);
        g.relaxUntilMs = now + static_cast<uint64_t>(Uniform(90.f, 200.f) * kRelaxMul[Intensity()] * 1000.f);
        g.peakSinceMs = 0;
        g.aiWaitsAtPeak = 0;
    }
    g.fired.push_front(Fired{now, &sc, by});
    while (g.fired.size() > kFiredKept) g.fired.pop_back();
    UE_LOGI("director: fired '%s' (%s, power %u) by %s -- tension %.0f -> %.0f, next threshold %.0f",
            sc.key, FlavorName(sc.flavor), static_cast<unsigned>(sc.power), by, before, g.tension, g.threshold);
}

// The rules' choice: a weighted draw over the rows allowed now, weighted by the night, by whether the
// players are split up or huddled, against repeating the last kind, and toward stronger rows the
// further the tension has run past its threshold.
const Scare* RulesPick(const Obs& o, uint64_t now) {
    size_t count = 0;
    const Scare* all = Catalog(&count);
    const Flavor* last = g.fired.empty() || !g.fired.front().scare ? nullptr : &g.fired.front().scare->flavor;
    const bool apart = o.apartM >= kApartM;
    const bool together = o.n >= 2 && o.apartM > 0.f && o.apartM < kTogetherM;
    const float excess = std::clamp((g.tension - g.threshold) / 30.f, 0.f, 1.f);
    float weights[kMaxRows] = {};
    float total = 0.f;
    for (size_t i = 0; i < count && i < kMaxRows; ++i) {
        const Scare& sc = all[i];
        if (!AllowedNow(sc, o, now)) continue;
        float w = 1.f;
        switch (sc.flavor) {
            case Flavor::Creature:   w *= o.night ? 1.6f : 0.5f; if (together) w *= 1.4f; break;
            case Flavor::Trap:       w *= o.night ? 1.f : 1.3f; if (apart) w *= 1.6f; break;
            case Flavor::Device:     w *= o.night ? 1.4f : 1.2f; if (apart) w *= 1.3f; break;
            case Flavor::Atmosphere: w *= o.night ? 0.8f : 1.f; break;
            case Flavor::Spectacle:  if (together) w *= 1.3f; break;
        }
        if (last && *last == sc.flavor) w *= 0.3f;
        if (sc.power >= 3) w *= 0.4f + excess;
        else if (sc.power <= 1) w *= 1.2f - 0.6f * excess;
        weights[i] = w;
        total += w;
    }
    if (total <= 0.f) return nullptr;
    float r = Uniform(0.f, total);
    for (size_t i = 0; i < count && i < kMaxRows; ++i) {
        if (weights[i] <= 0.f) continue;
        if (r < weights[i]) return &all[i];
        r -= weights[i];
    }
    return nullptr;
}

// ---- the AI agent ----------------------------------------------------------------------------------
std::string ApiKey() {
    std::string key = CFG::ResolveString(CR::rows::director_ai_key);
    if (key.empty()) key = CFG::ReadEnv("ANTHROPIC_API_KEY");
    return key;
}

bool AiUsable(uint64_t now) {
    return CFG::ResolveFlag(CR::rows::director_ai) && now >= g.aiBackoffUntilMs && !ApiKey().empty();
}

std::string Situation(const Obs& o, uint64_t now) {
    json j;
    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", o.hour, o.minute);
    j["time"] = clock;
    j["day"] = o.day;
    j["night"] = o.night;
    j["tension"] = static_cast<int>(g.tension);
    j["threshold"] = static_cast<int>(g.threshold);
    j["peak"] = g.tension >= g.threshold;
    j["relaxing"] = now < g.relaxUntilMs;
    j["event_running"] = o.activeEvents > 0;
    json players = json::array();
    for (int i = 0; i < o.n; ++i) {
        const Seen& p = o.p[i];
        json pj;
        pj["name"] = NameOf(p.slot);
        pj["state"] = RestName(p.rest, p.alive);
        if (p.nearestM >= 0.f) {
            pj["meters_to_nearest_player"] = static_cast<int>(p.nearestM);
            pj["alone"] = p.nearestM >= kApartM;
        }
        players.push_back(pj);
    }
    j["players"] = players;
    json recent = json::array();
    for (const Fired& f : g.fired) {
        if (!f.scare) continue;
        recent.push_back({{"key", f.scare->key}, {"kind", FlavorName(f.scare->flavor)},
                          {"minutes_ago", static_cast<int>((now - f.atMs) / 60000ull)}});
    }
    j["recent"] = recent;
    json avail = json::array();
    size_t count = 0;
    const Scare* all = Catalog(&count);
    for (size_t i = 0; i < count; ++i)
        if (AllowedNow(all[i], o, now))
            avail.push_back({{"key", all[i].key}, {"kind", FlavorName(all[i].flavor)},
                             {"power", all[i].power}, {"what", all[i].what}});
    j["available"] = avail;
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

void SendEmail(const std::string& subject, const std::string& body, uint64_t now) {
    if (!ue_wrap::email::EnsureResolved()) return;
    ue_wrap::email::Row row;
    row.username = kEmailSender;
    row.topic = coop::text::FromUtf8Lossy(subject.c_str());
    row.text = coop::text::FromUtf8Lossy(body.c_str());
    const ue_wrap::RowApply r = ue_wrap::email::AddEmail(row);
    if (r.result == ue_wrap::RowApplyResult::Applied) {
        g.lastEmailMs = now;
        UE_LOGI("director: the agent sent an email ('%s')", subject.c_str());
    } else {
        UE_LOGW("director: the agent's email was not added (%s)", r.why);
    }
}

void SetAiState(const std::string& s) { g.aiState = s; }

// The agent's answer, when one arrived: an email if it wrote one and the gap allows, then its choice,
// held to the rules' own conditions.
void TakeAnswer(const Obs& o, bool holding, uint64_t now) {
    ai::Answer a;
    if (!ai::Take(a)) return;
    if (!a.ok) {
        if (++g.aiFails >= kAiFailLimit) {
            g.aiBackoffUntilMs = now + kAiBackoffMs;
            g.aiFails = 0;
            SetAiState("failing (" + a.error + "); the rules decide for 5 minutes");
        } else {
            SetAiState("no answer: " + a.error);
        }
        return;
    }
    g.aiFails = 0;
    if (!a.note.empty()) g.aiNote = a.note;
    if (!a.body.empty() && CFG::ResolveFlag(CR::rows::director_ai_emails) &&
        (g.lastEmailMs == 0 || now - g.lastEmailMs >= kEmailGapMs))
        SendEmail(a.subject, a.body, now);
    if (!a.fire) {
        SetAiState("waits");
        if (g.tension >= g.threshold) ++g.aiWaitsAtPeak;
        return;
    }
    const Scare* sc = FindScare(a.event.c_str());
    if (!sc || !AllowedNow(*sc, o, now)) {
        SetAiState("named '" + a.event + "', which is not allowed now");
        return;
    }
    if (holding || now < g.relaxUntilMs || g.tension < kAiMinTension) {
        SetAiState("chose '" + a.event + "' too early; held");
        return;
    }
    SetAiState(std::string("fired '") + sc->key + "'");
    Fire(*sc, "agent", now);
}

// ---- the tick --------------------------------------------------------------------------------------
std::string HoldReason(const Obs& o) {
    if (!o.gameplay) return "the world is loading";
    if (o.joining) return "a player is joining";
    if (o.anyResting && CFG::ResolveFlag(CR::rows::director_respect_sleep)) return "someone is asleep";
    if (o.activeEvents > 0) return "one of the game's own events is running";
    return std::string();
}

void PublishView(bool hosting, bool enabled, uint64_t now) {
    std::lock_guard<std::mutex> lock(g_viewMu);
    View& v = g_view;
    v.hosting = hosting;
    v.enabled = enabled;
    v.tension = g.tension;
    v.threshold = g.threshold;
    v.relaxLeftS = now < g.relaxUntilMs ? static_cast<int>((g.relaxUntilMs - now) / 1000ull) : 0;
    std::snprintf(v.hold, sizeof(v.hold), "%s", g.hold.c_str());
    v.aiOn = CFG::ResolveFlag(CR::rows::director_ai) && !ApiKey().empty();
    std::snprintf(v.aiState, sizeof(v.aiState), "%s", g.aiState.c_str());
    std::snprintf(v.aiNote, sizeof(v.aiNote), "%s", g.aiNote.c_str());
    v.lineCount = 0;
    for (const Fired& f : g.fired) {
        if (!f.scare || v.lineCount >= View::kLines) continue;
        std::snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "%3u min ago  %-18s %-10s by %s",
                      static_cast<unsigned>((now - f.atMs) / 60000ull), f.scare->key, FlavorName(f.scare->flavor),
                      f.by);
    }
}

coop::net::Session* HostSession() {
    auto* s = g_session.load(std::memory_order_acquire);
    return (s && s->running() && s->role() == coop::net::Role::Host) ? s : nullptr;
}

void Step(coop::net::Session& s, uint64_t now) {
    const float dt = g.lastTickMs ? std::min(5.f, static_cast<float>(now - g.lastTickMs) / 1000.f) : 1.f;
    g.lastTickMs = now;
    const Obs o = Observe(s);
    g.hold = HoldReason(o);
    const bool holding = !g.hold.empty();

    // The climb. A join or a load stops it; sleep and a breather slow it.
    if (o.gameplay && !o.joining) {
        float gain = kGain[Intensity()];
        if (o.night) gain *= kNightMul;
        else if (o.dusk) gain *= kDuskMul;
        if (o.apartM >= kApartM) gain *= kApartMul;
        if (o.anyResting && CFG::ResolveFlag(CR::rows::director_respect_sleep)) gain *= kRestMul;
        if (now < g.relaxUntilMs) gain *= kRelaxGainMul;
        g.tension = std::min(100.f, g.tension + gain * dt);
    }
    const bool peak = !holding && now >= g.relaxUntilMs && g.tension >= g.threshold;
    if (peak && g.peakSinceMs == 0) g.peakSinceMs = now;
    if (!peak) g.peakSinceMs = 0;

    const bool aiUsable = AiUsable(now);
    TakeAnswer(o, holding, now);
    if (aiUsable && o.gameplay && !o.joining && !ai::Busy()) {
        const uint64_t period = static_cast<uint64_t>(CFG::ResolveInt(CR::rows::director_ai_period_s)) * 1000ull;
        // A peak asks at once (once per ten seconds at most); otherwise the agent looks in at its period,
        // and only once the night has some tension to work with.
        const bool due = peak ? now - g.lastAskMs >= 10000 : (now - g.lastAskMs >= period && g.tension >= 25.f);
        if (due && ai::Ask(ApiKey(), CFG::ResolveString(CR::rows::director_ai_model),
                           CFG::ResolveString(CR::rows::director_ai_language), CFG::ResolveFlag(CR::rows::director_ai_emails),
                           Situation(o, now))) {
            g.lastAskMs = now;
            SetAiState("thinking...");
        }
    }
    if (!peak) return;
    // The agent owns the peak while it is healthy, for a bounded wait; at the top of the meter it does
    // not get to stall forever.
    const bool agentHasIt = aiUsable && now - g.peakSinceMs < kAiPeakWaitMs &&
                            !(g.tension >= 99.f && g.aiWaitsAtPeak >= 2);
    if (agentHasIt) return;
    if (const Scare* sc = RulesPick(o, now)) Fire(*sc, "rules", now);
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void Tick() {
    const uint64_t now = coop::net::NowMs();
    if (now - g.lastTickMs < kTickMs && g.lastTickMs != 0) return;
    coop::net::Session* s = HostSession();
    const bool enabled = CFG::ResolveFlag(CR::rows::director_enabled);
    if (!s || !enabled) {
        g.lastTickMs = now;
        g.hold = s ? "turned off (director.enabled)" : "only the host runs the director";
        PublishView(s != nullptr, enabled, now);
        return;
    }
    if (!g.seeded) {
        g.rng.seed(static_cast<uint32_t>(now ^ (now >> 32)));
        g.threshold = Uniform(55.f, 85.f);
        g.seeded = true;
    }
    Step(*s, now);
    PublishView(true, true, now);
}

void OnDisconnect() {
    g = State{};
    std::lock_guard<std::mutex> lock(g_viewMu);
    g_view = View{};
}

void ReadView(View& out) {
    std::lock_guard<std::mutex> lock(g_viewMu);
    out = g_view;
}

void RequestFire(const char* key) {
    const Scare* sc = FindScare(key);
    if (!sc) return;
    GT::Post([sc] {
        if (!HostSession()) return;
        Fire(*sc, "test", coop::net::NowMs());
    });
}

void RequestPick() {
    GT::Post([] {
        coop::net::Session* s = HostSession();
        if (!s) return;
        const uint64_t now = coop::net::NowMs();
        const Obs o = Observe(*s);
        if (const Scare* sc = RulesPick(o, now)) Fire(*sc, "rules", now);
        else UE_LOGI("director: a pick was asked for and no scare is allowed right now");
    });
}

void RequestCalm() {
    GT::Post([] {
        g.tension = 0.f;
        g.relaxUntilMs = coop::net::NowMs() + 120000;
        g.peakSinceMs = 0;
        UE_LOGI("director: calmed by the host");
    });
}

}  // namespace coop::fear
