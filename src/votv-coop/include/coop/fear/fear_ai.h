// coop/fear/fear_ai.h -- the fear director's AI agent: one Claude API request at a time.
//
// The director hands it a JSON picture of the night (the clock, the players, the tension, the scares
// it may fire now and the ones it fired lately); the agent answers with one decision: fire a named
// scare or wait, optionally an email for the base computer, and one sentence about its plan for the
// host's log. The request runs on a detached worker (a network round trip must never stall a frame)
// and the answer waits here until the director's next tick takes it.
//
// Only the host asks, and only when the host turned director.ai on and gave it a key: the request goes
// to api.anthropic.com, carries that key and the situation (the players' nicknames, the clock, the
// tension), and nothing else. The key is a credential row: it never reaches the log or another peer.

#pragma once

#include <string>

namespace coop::fear::ai {

struct Answer {
    bool ok = false;        // the request returned and the reply parsed into a decision
    bool fire = false;      // the decision: fire `event`, or wait
    std::string event;      // a catalog key, as the agent wrote it (the director validates it)
    std::string subject;    // an email's subject, empty for none
    std::string body;       // an email's body, empty for none
    std::string note;       // the agent's sentence about its plan
    std::string error;      // why ok is false: transport, HTTP status or an unreadable reply
};

// A request is in flight.
bool Busy();

// Start one request on a worker thread. `situation` is the JSON object the director built; `language`
// names the language the agent writes emails in; `emails` says whether it may write one at all. False,
// and nothing started, while a request is in flight or with an empty key or model.
bool Ask(const std::string& apiKey, const std::string& model, const std::string& language, bool emails,
         const std::string& situation);

// The finished request's answer, handed over once. False while none is waiting.
bool Take(Answer& out);

}  // namespace coop::fear::ai
