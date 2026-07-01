#pragma once
#include <string>
#include <chrono>
#include <cstdint>

enum class QuotaState {
    Loading,
    Live,
    Refreshing,
    CodexNotFound,
    CodexNotLoggedIn,
    ReadTimeout,
    ReadFailed,
    NoQuotaData,
};

struct WindowQuota {
    bool   valid = false;
    double remainingPercent = 0.0;   // 0-100
    double usedPercent = 0.0;
    std::chrono::system_clock::time_point resetsAt;
};

struct QuotaModel {
    QuotaState state = QuotaState::Loading;
    WindowQuota primary;              // 5 h window
    WindowQuota secondary;            // 7 d window
    std::chrono::system_clock::time_point lastRefresh;
    std::string lastError;
    bool stale = false;               // cached data after failed refresh

    // Cached last-known-good result (never written to disk)
    bool hasPreviousSuccess = false;
    WindowQuota prevPrimary;
    WindowQuota prevSecondary;
    std::chrono::system_clock::time_point prevRefresh;

    /// Parse the raw JSON response from account/rateLimits/read.
    /// Returns false when the message signals an error (not-logged-in, etc.).
    bool parseFromJson(const std::string& json);

    std::wstring tooltipText() const;
    std::wstring copyStatusText() const;
};
