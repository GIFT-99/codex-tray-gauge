#pragma once
#include <string>
#include <memory>

struct CodexResult {
    bool        success = false;
    std::string json;         // raw response body (JSON)
    std::string error;        // human-readable error description
};

class CodexClient {
public:
    /// Find codex.exe via CODEX_CLI_PATH → %LOCALAPPDATA%\OpenAI\Codex\bin → PATH.
    /// Returns empty string if not found.
    static std::wstring findCodexPath();

    /// Spawn codex app-server, send initialize + rateLimits/read, return the
    /// raw JSON response.  Blocks for up to 12 s.
    static CodexResult readQuota();
};
