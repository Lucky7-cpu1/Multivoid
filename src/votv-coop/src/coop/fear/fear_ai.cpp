// coop/fear/fear_ai.cpp -- see coop/fear/fear_ai.h.

#include "coop/fear/fear_ai.h"

#include "coop/fear/fear_http.h"

#include "ue_wrap/core/log.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace coop::fear::ai {
namespace {

using json = nlohmann::json;

constexpr int kTimeoutMs = 25000;
constexpr int kMaxTokens = 400;
// What the director keeps of the agent's words: a subject line, a short email, one sentence.
constexpr size_t kSubjectMax = 60;
constexpr size_t kBodyMax = 400;
constexpr size_t kNoteMax = 220;

std::atomic<bool> g_busy{false};
std::mutex g_mu;          // guards the two below
bool g_haveAnswer = false;
Answer g_answer;

// The standing instructions. The situation arrives as the user turn; the reply is one JSON object.
std::string SystemPrompt(const std::string& language, bool emails) {
    std::string p =
        "You are the fear director of a co-op session of Voices of the Void, a slow-burn horror game. "
        "The players run a lonely radio-telescope base in a forest; at night strange things happen around "
        "it. Your craft is pacing fear like a horror film director: long quiet build-up, a scare at the "
        "worst possible moment, then a breather so the next one lands.\n"
        "Each turn you receive a JSON snapshot: the game clock, the players (their state, how far apart "
        "they are, who is alone), the tension meter (0-100) and its threshold, whether one of the game's "
        "own events is running, the scares you fired lately, and `available`: the only scares you may "
        "fire right now, each with a key, a kind, a power (1 unsettling, 2 a jolt, 3 something in the "
        "room) and what it does. Every player sees every scare.\n"
        "Guidelines:\n"
        "- Below the threshold you usually wait; at or above it (`peak` true) you usually act.\n"
        "- A player who is alone or far from the others is the most vulnerable: traps and devices work "
        "best on them. Creatures land hardest when the group feels safe together.\n"
        "- Vary the kind: do not use the same kind twice in a row, and keep power 3 rare.\n"
        "- The night is yours; in daylight prefer the quiet, wrong things (devices, traps).\n"
        "- Never fire a key that is not in `available`.\n";
    if (emails) {
        p += "You may also leave an email on the base computer, which every player reads. Keep it in the "
             "world: an automated system notice that is subtly wrong, a stranger who should not know "
             "where they are, something that should not be able to write. Never mention AI, a game, "
             "players, a mod, tension or a director. At most two sentences and 280 characters; the subject "
             "under 40 characters. Write it in ";
        p += language.empty() ? std::string("English") : language;
        p += ". Most turns send no email; about one turn in four at most.\n";
    } else {
        p += "Emails are off this session: always answer \"email\": null.\n";
    }
    p += "Answer with exactly one JSON object and nothing else, no markdown:\n"
         "{\"action\":\"fire\" or \"wait\",\"event\":\"<an available key, or empty>\","
         "\"email\":{\"subject\":\"...\",\"body\":\"...\"} or null,"
         "\"note\":\"<one short sentence: your plan, for the host's log>\"}";
    return p;
}

std::string Cut(std::string s, size_t max) {
    if (s.size() <= max) return s;
    s.resize(max);
    // Do not leave half a UTF-8 sequence at the cut.
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0x80)) s.pop_back();
    return s;
}

std::string StringField(const json& j, const char* name) {
    auto it = j.find(name);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

// The Messages reply's text, then the decision object inside it (the first '{' to the last '}', so a
// stray sentence or a code fence around it does not cost the answer).
Answer Parse(const http::Reply& reply) {
    Answer a;
    if (!reply.ok) { a.error = reply.error; return a; }
    const json root = json::parse(reply.body, nullptr, false);
    if (reply.status != 200) {
        std::string msg;
        if (!root.is_discarded() && root.is_object()) {
            auto e = root.find("error");
            if (e != root.end() && e->is_object()) msg = StringField(*e, "message");
        }
        a.error = "HTTP " + std::to_string(reply.status) + (msg.empty() ? std::string() : ": " + Cut(msg, 160));
        return a;
    }
    if (root.is_discarded() || !root.is_object()) { a.error = "the reply was not JSON"; return a; }
    std::string text;
    auto content = root.find("content");
    if (content != root.end() && content->is_array())
        for (const json& block : *content)
            if (block.is_object() && StringField(block, "type") == "text") text += StringField(block, "text");
    const size_t open = text.find('{');
    const size_t close = text.rfind('}');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        a.error = "the agent answered without a decision";
        return a;
    }
    const json d = json::parse(text.substr(open, close - open + 1), nullptr, false);
    if (d.is_discarded() || !d.is_object()) { a.error = "the agent's decision was not JSON"; return a; }
    a.fire = StringField(d, "action") == "fire";
    a.event = Cut(StringField(d, "event"), 64);
    a.note = Cut(StringField(d, "note"), kNoteMax);
    auto email = d.find("email");
    if (email != d.end() && email->is_object()) {
        a.subject = Cut(StringField(*email, "subject"), kSubjectMax);
        a.body = Cut(StringField(*email, "body"), kBodyMax);
        if (a.subject.empty() || a.body.empty()) {
            a.subject.clear();
            a.body.clear();
        }
    }
    a.ok = true;
    return a;
}

void Publish(Answer a) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_answer = std::move(a);
    g_haveAnswer = true;
}

// The worker's whole body. Nothing may escape a detached thread, and the busy flag must fall on every
// path, so the body is caught whole.
void Run(std::string key, std::string body) {
    Answer a;
    try {
        a = Parse(http::PostMessages(key, body, kTimeoutMs));
    } catch (...) {
        a = Answer{};
        a.error = "the request failed inside the mod";
    }
    if (!a.ok) UE_LOGW("director_ai: no decision -- %s", a.error.c_str());
    Publish(std::move(a));
    g_busy.store(false, std::memory_order_release);
}

}  // namespace

bool Busy() { return g_busy.load(std::memory_order_acquire); }

bool Ask(const std::string& apiKey, const std::string& model, const std::string& language, bool emails,
         const std::string& situation) {
    if (apiKey.empty() || model.empty()) return false;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
    std::string body;
    try {
        json req = {
            {"model", model},
            {"max_tokens", kMaxTokens},
            {"system", SystemPrompt(language, emails)},
            {"messages", json::array({ json{{"role", "user"}, {"content", situation}} })},
        };
        body = req.dump(-1, ' ', false, json::error_handler_t::replace);
        std::thread(Run, apiKey, std::move(body)).detach();
    } catch (...) {
        g_busy.store(false, std::memory_order_release);
        UE_LOGW("director_ai: the request could not be started");
        return false;
    }
    return true;
}

bool Take(Answer& out) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_haveAnswer) return false;
    out = std::move(g_answer);
    g_answer = Answer{};
    g_haveAnswer = false;
    return true;
}

}  // namespace coop::fear::ai
