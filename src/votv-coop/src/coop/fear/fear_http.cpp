// coop/fear/fear_http.cpp -- see coop/fear/fear_http.h.

#include "coop/fear/fear_http.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace coop::fear::http {
namespace {

constexpr wchar_t kHost[] = L"api.anthropic.com";
constexpr wchar_t kPath[] = L"/v1/messages";
constexpr wchar_t kVersion[] = L"2023-06-01";
// A decision is a few hundred bytes; an error page a few more. Anything past this is not ours.
constexpr size_t kBodyCap = 256u * 1024u;

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET hh) : h(hh) {}
    ~Handle() { if (h) ::WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

std::wstring WidenAscii(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return w;
}

// A key is printable ASCII; anything else would break the header line, so it is refused before use.
bool KeyUsable(const std::string& key) {
    if (key.empty() || key.size() > 512) return false;
    for (char c : key)
        if (c <= 0x20 || c >= 0x7F) return false;
    return true;
}

std::string LastError(const char* what) {
    return std::string(what) + " failed (WinHTTP error " + std::to_string(::GetLastError()) + ")";
}

}  // namespace

Reply PostMessages(const std::string& apiKey, const std::string& jsonBody, int timeoutMs) {
    Reply out;
    if (!KeyUsable(apiKey)) {
        out.error = "the API key is empty or holds characters a key cannot have";
        return out;
    }
    Handle session(::WinHttpOpen(L"multivoid-director/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { out.error = LastError("WinHttpOpen"); return out; }
    ::WinHttpSetTimeouts(session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    Handle conn(::WinHttpConnect(session.h, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!conn) { out.error = LastError("WinHttpConnect"); return out; }
    Handle req(::WinHttpOpenRequest(conn.h, L"POST", kPath, nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req) { out.error = LastError("WinHttpOpenRequest"); return out; }

    const std::wstring headers = std::wstring(L"Content-Type: application/json\r\n") +
                                 L"anthropic-version: " + kVersion + L"\r\n" +
                                 L"x-api-key: " + WidenAscii(apiKey) + L"\r\n";
    if (!::WinHttpSendRequest(req.h, headers.c_str(), static_cast<DWORD>(-1L),
                              const_cast<char*>(jsonBody.data()), static_cast<DWORD>(jsonBody.size()),
                              static_cast<DWORD>(jsonBody.size()), 0)) {
        out.error = ::GetLastError() == ERROR_WINHTTP_SECURE_FAILURE
                        ? std::string("TLS validation of api.anthropic.com failed")
                        : LastError("sending the request");
        return out;
    }
    if (!::WinHttpReceiveResponse(req.h, nullptr)) { out.error = LastError("receiving the response"); return out; }

    DWORD status = 0, statusLen = sizeof(status);
    if (!::WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen, WINHTTP_NO_HEADER_INDEX)) {
        out.error = LastError("reading the status");
        return out;
    }
    out.status = static_cast<int>(status);

    for (;;) {
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(req.h, &avail)) { out.error = LastError("reading the body"); return out; }
        if (avail == 0) break;
        size_t chunk = avail;
        if (out.body.size() + chunk > kBodyCap) chunk = kBodyCap - out.body.size();
        if (chunk == 0) break;
        const size_t base = out.body.size();
        out.body.resize(base + chunk);
        DWORD read = 0;
        if (!::WinHttpReadData(req.h, &out.body[base], static_cast<DWORD>(chunk), &read)) {
            out.error = LastError("reading the body");
            return out;
        }
        out.body.resize(base + read);
        if (read == 0) break;
    }
    out.ok = true;
    return out;
}

}  // namespace coop::fear::http
