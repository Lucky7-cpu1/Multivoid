// coop/fear/fear_http.h -- the AI director's one outbound call: a Messages API request.
//
// A blocking WinHTTP POST to https://api.anthropic.com/v1/messages with the host's key in the
// x-api-key header. Its own small client rather than coop/net/http_client, which is the master
// server's (fixed headers, no proxy by design): this request needs its headers and goes through the
// system's proxy settings like any other HTTPS call the player's machine makes. TLS is WinHTTP's
// default validation, no relaxation. Worker threads only.

#pragma once

#include <string>

namespace coop::fear::http {

struct Reply {
    bool ok = false;      // a complete HTTP response arrived
    int status = 0;       // its status code
    std::string body;     // the response body, capped
    std::string error;    // why ok is false
};

// POST `jsonBody` to the Messages endpoint. `timeoutMs` bounds each phase (resolve, connect, send,
// receive). Never throws.
Reply PostMessages(const std::string& apiKey, const std::string& jsonBody, int timeoutMs);

}  // namespace coop::fear::http
