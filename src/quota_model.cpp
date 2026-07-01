#include "quota_model.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <cstdlib>

// Portable safe swprintf
#ifdef _MSC_VER
#define SWPRINTF _snwprintf_s
#else
#define SWPRINTF swprintf
#endif

// _mkgmtime (MSVC) vs timegm (POSIX) — always on Windows, use _mkgmtime
#ifdef _WIN32
static inline time_t portable_mkgmtime(struct tm* tm) { return _mkgmtime(tm); }
#else
static inline time_t portable_mkgmtime(struct tm* tm) { return timegm(tm); }
#endif

// localtime_s (C11/Windows) vs localtime_r (POSIX) — args swapped!
#ifdef _WIN32
#define LOCALTIME_S(tm, t) localtime_s(tm, t)
#else
#define LOCALTIME_S(tm, t) localtime_r(t, tm)
#endif

using json = nlohmann::json;
namespace chrono = std::chrono;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double clampPct(double v) {
    if (v < 0.0) return 0.0;
    if (v > 100.0) return 100.0;
    return v;
}

static bool parseWindow(const json& j, WindowQuota& w) {
    if (!j.is_object()) return false;
    auto u = j.find("usedPercent");
    if (u == j.end() || !u->is_number()) return false;
    w.usedPercent = u->get<double>();
    w.remainingPercent = clampPct(100.0 - w.usedPercent);

    auto ra = j.find("resetsAt");
    if (ra != j.end() && ra->is_string()) {
        std::string ts = ra->get<std::string>();
        // ISO-8601: "2026-06-30T14:32:00Z" or with offset
        std::tm tm = {};
        std::istringstream ss(ts);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!ss.fail()) {
            // mktime assumes local time — crude but good enough for display
            std::time_t tt = portable_mkgmtime(&tm);
            w.resetsAt = chrono::system_clock::from_time_t(tt);
        } else {
            w.resetsAt = chrono::system_clock::time_point{};
        }
    }
    w.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// parseFromJson
// ---------------------------------------------------------------------------

bool QuotaModel::parseFromJson(const std::string& raw) {
    // Clear volatile state but keep previous success
    state = QuotaState::NoQuotaData;
    primary = WindowQuota{};
    secondary = WindowQuota{};
    lastError.clear();
    stale = false;

    try {
        json j = json::parse(raw);

        // Check for JSON-RPC error
        if (j.contains("error")) {
            auto& err = j["error"];
            std::string msg = err.value("message", "");
            if (msg.find("not logged in") != std::string::npos ||
                msg.find("authentication") != std::string::npos ||
                msg.find("unauthorized") != std::string::npos) {
                state = QuotaState::CodexNotLoggedIn;
                lastError = msg;
            } else {
                state = QuotaState::ReadFailed;
                lastError = msg;
            }
            return false;
        }

        // Navigate result.rateLimitsByLimitId.codex (preferred)
        const json* rl = nullptr;
        if (j.contains("result")) {
            auto& result = j["result"];
            auto it = result.find("rateLimitsByLimitId");
            if (it != result.end() && it->is_object()) {
                auto c = it->find("codex");
                if (c != it->end()) rl = &*c;
            }
            // fallback: result.rateLimits
            if (!rl) {
                auto fb = result.find("rateLimits");
                if (fb != result.end()) rl = &*fb;
            }
        }

        if (!rl || !rl->is_object()) {
            state = QuotaState::NoQuotaData;
            lastError = "unexpected response structure";
            return false;
        }

        if (!parseWindow(rl->value("primary", json::object()), primary) &&
            !parseWindow(rl->value("shortTerm",  json::object()), primary)) {
            lastError = "primary window parse failed";
        }
        if (!parseWindow(rl->value("secondary",  json::object()), secondary) &&
            !parseWindow(rl->value("longTerm",   json::object()), secondary)) {
            // secondary is optional — primary-only is still useful
        }

        lastRefresh = chrono::system_clock::now();
        state = QuotaState::Live;
        stale = false;

        // Cache for later fallback
        hasPreviousSuccess = true;
        prevPrimary = primary;
        prevSecondary = secondary;
        prevRefresh = lastRefresh;
        return true;

    } catch (const json::exception& e) {
        state = QuotaState::ReadFailed;
        lastError = std::string("json parse: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// formatting helpers
// ---------------------------------------------------------------------------

static std::wstring fmtPct(double v) {
    wchar_t buf[16];
    SWPRINTF(buf, 16, L"%.0f%%", v);
    return buf;
}

static std::wstring fmtCountdown(const chrono::system_clock::time_point& tp) {
    if (tp.time_since_epoch().count() == 0) return L"unknown";
    auto now = chrono::system_clock::now();
    if (tp <= now) return L"now";
    auto remain = chrono::duration_cast<chrono::seconds>(tp - now);
    auto sec = remain.count();
    if (sec < 0) return L"now";
    auto d = sec / 86400;  sec %= 86400;
    auto h = sec / 3600;   sec %= 3600;
    auto m = sec / 60;

    wchar_t buf[64];
    if (d > 0)
        SWPRINTF(buf, 64, L"%lldd %02lldh", d, h);
    else if (h > 0)
        SWPRINTF(buf, 64, L"%lldh %02lldm", h, m);
    else
        SWPRINTF(buf, 64, L"%lldm", m);
    return buf;
}

static std::wstring fmtLocalTime(const chrono::system_clock::time_point& tp) {
    if (tp.time_since_epoch().count() == 0) return L"n/a";
    std::time_t t = chrono::system_clock::to_time_t(tp);
    std::tm local;
    LOCALTIME_S(&local, &t);
    wchar_t buf[32];
    SWPRINTF(buf, 32, L"%04d-%02d-%02d %02d:%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min);
    return buf;
}

static std::wstring fmtHhMm(const chrono::system_clock::time_point& tp) {
    if (tp.time_since_epoch().count() == 0) return L"--:--";
    std::time_t t = chrono::system_clock::to_time_t(tp);
    std::tm local;
    LOCALTIME_S(&local, &t);
    wchar_t buf[8];
    SWPRINTF(buf, 8, L"%02d:%02d", local.tm_hour, local.tm_min);
    return buf;
}

// ---------------------------------------------------------------------------
// tooltip / copy
// ---------------------------------------------------------------------------

std::wstring QuotaModel::tooltipText() const {
    std::wstring s = L"Codex Quota";

    if (state == QuotaState::CodexNotFound) {
        s += L"\nCodex not found";
        return s;
    }
    if (state == QuotaState::CodexNotLoggedIn) {
        s += L"\nNot logged in";
        if (!lastError.empty()) { s += L"\n"; s += std::wstring(lastError.begin(), lastError.end()); }
        return s;
    }
    if (state == QuotaState::ReadTimeout) {
        s += L"\nRead timed out";
        return s;
    }
    if (state == QuotaState::NoQuotaData) {
        s += L"\nNo quota data";
        return s;
    }
    if (state == QuotaState::Loading) {
        s += L"\nLoading...";
        return s;
    }

    // Live / Refreshing / ReadFailed
    if (stale && hasPreviousSuccess) {
        s += L"\nLast known:";
    }

    if (primary.valid) {
        s += L"\n5h: " + fmtPct(primary.remainingPercent) + L" left";
        if (primary.resetsAt.time_since_epoch().count() > 0)
            s += L", resets in " + fmtCountdown(primary.resetsAt);
    } else {
        s += L"\n5h: --";
    }

    if (secondary.valid) {
        s += L"\n7d: " + fmtPct(secondary.remainingPercent) + L" left";
        if (secondary.resetsAt.time_since_epoch().count() > 0)
            s += L", resets in " + fmtCountdown(secondary.resetsAt);
    } else {
        s += L"\n7d: --";
    }

    if (stale) {
        s += L"\n(using cached data)";
    }

    s += L"\nLast refresh: " + fmtHhMm(lastRefresh);
    if (!lastError.empty()) {
        s += L"\n";
        s += std::wstring(lastError.begin(), lastError.end());
    }
    return s;
}

std::wstring QuotaModel::copyStatusText() const {
    std::wstring s;
    if (primary.valid) {
        s += L"Codex 5h: " + fmtPct(primary.remainingPercent) + L" left";
        if (primary.resetsAt.time_since_epoch().count() > 0)
            s += L", resets at " + fmtLocalTime(primary.resetsAt);
    } else {
        s += L"Codex 5h: n/a";
    }
    s += L"\n";
    if (secondary.valid) {
        s += L"Codex 7d: " + fmtPct(secondary.remainingPercent) + L" left";
        if (secondary.resetsAt.time_since_epoch().count() > 0)
            s += L", resets at " + fmtLocalTime(secondary.resetsAt);
    } else {
        s += L"Codex 7d: n/a";
    }
    s += L"\nLast refresh: " + fmtLocalTime(lastRefresh);
    if (!lastError.empty()) {
        s += L"\nError: ";
        s += std::wstring(lastError.begin(), lastError.end());
    }
    return s;
}
