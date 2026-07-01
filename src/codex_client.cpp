#include "codex_client.h"
#include <windows.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// findCodexPath
// ---------------------------------------------------------------------------

std::wstring CodexClient::findCodexPath() {
    wchar_t buf[MAX_PATH];

    // 1. CODEX_CLI_PATH env var
    DWORD len = GetEnvironmentVariableW(L"CODEX_CLI_PATH", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES)
        return buf;

    // 2. %LOCALAPPDATA%\OpenAI\Codex\bin\codex.exe (and subdirs for versioned installs)
    if (ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", buf, MAX_PATH) > 0) {
        std::wstring p(buf);
        p += L"\\OpenAI\\Codex\\bin\\codex.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;

        // 2b. Search subdirectories (e.g. bin/aec6b7c6fcdfb66a/codex.exe)
        std::wstring searchDir(buf);
        searchDir += L"\\OpenAI\\Codex\\bin\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchDir.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
                    fd.cFileName[0] != L'.') {
                    std::wstring candidate(buf);
                    candidate += L"\\OpenAI\\Codex\\bin\\";
                    candidate += fd.cFileName;
                    candidate += L"\\codex.exe";
                    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        FindClose(hFind);
                        return candidate;
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    // 3. PATH lookup
    DWORD searchRet = SearchPathW(nullptr, L"codex.exe", nullptr, MAX_PATH, buf, nullptr);
    if (searchRet > 0 && searchRet < MAX_PATH)
        return buf;

    return {};
}

// ---------------------------------------------------------------------------
// Pipe helpers
// ---------------------------------------------------------------------------

struct PipePair {
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    void close() {
        if (rd) { CloseHandle(rd); rd = nullptr; }
        if (wr) { CloseHandle(wr); wr = nullptr; }
    }
};

static bool createPipe(PipePair& p) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    return CreatePipe(&p.rd, &p.wr, &sa, 0) != 0;
}

// ---------------------------------------------------------------------------
// JSON Lines helpers
// ---------------------------------------------------------------------------

static bool writeJsonLine(HANDLE h, const std::string& body) {
    std::string line = body + "\n";
    DWORD written = 0;
    return WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

static std::string readJsonLine(HANDLE h, DWORD timeoutMs, bool& timedOut) {
    timedOut = false;
    auto start = GetTickCount64();
    std::string line;
    char ch;
    DWORD n = 0;
    while (true) {
        if (GetTickCount64() - start > timeoutMs) { timedOut = true; return {}; }

        DWORD available = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr))
            return {};
        if (available == 0) {
            Sleep(5);
            continue;
        }

        if (!ReadFile(h, &ch, 1, &n, nullptr) || n == 0) {
            Sleep(5);
            continue;
        }
        line += ch;
        if (!line.empty() && line.back() == '\n') break;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    return line;
}

// ---------------------------------------------------------------------------
// readQuota  (uses "codex app-server --listen stdio://" + account/rateLimits/read)
// ---------------------------------------------------------------------------

CodexResult CodexClient::readQuota() {
    CodexResult result;

    std::wstring exe = findCodexPath();
    if (exe.empty()) {
        result.error = "Codex not found";
        return result;
    }

    PipePair stdinPipe, stdoutPipe, stderrPipe;
    if (!createPipe(stdinPipe) || !createPipe(stdoutPipe) || !createPipe(stderrPipe)) {
        result.error = "pipe creation failed";
        return result;
    }

    SetHandleInformation(stdinPipe.wr,  HANDLE_FLAG_INHERIT, FALSE);
    SetHandleInformation(stdoutPipe.rd, HANDLE_FLAG_INHERIT, FALSE);
    SetHandleInformation(stderrPipe.rd, HANDLE_FLAG_INHERIT, FALSE);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = stdinPipe.rd;
    si.hStdOutput = stdoutPipe.wr;
    si.hStdError  = stderrPipe.wr;

    PROCESS_INFORMATION pi = {};

    // Use "codex app-server --listen stdio://" (standard JSON-RPC over stdio)
    std::wstring cmdLine = L"\"" + exe + L"\" app-server --listen stdio://";
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        result.error = "failed to start codex app-server (error " +
                       std::to_string(GetLastError()) + ")";
        return result;
    }

    CloseHandle(stdinPipe.rd);  stdinPipe.rd  = nullptr;
    CloseHandle(stdoutPipe.wr); stdoutPipe.wr = nullptr;
    CloseHandle(stderrPipe.wr); stderrPipe.wr = nullptr;

    auto cleanup = [&]() {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        stdinPipe.close();
        stdoutPipe.close();
        stderrPipe.close();
    };

    // 1. initialize
    std::string initReq = R"({"id":1,"method":"initialize","params":{"clientInfo":{"name":"codex-tray-gauge","version":"1.0.0"}}})";
    if (!writeJsonLine(stdinPipe.wr, initReq)) {
        cleanup();
        result.error = "write initialize failed";
        return result;
    }

    // 2. account/rateLimits/read
    std::string rateReq = R"({"id":2,"method":"account/rateLimits/read"})";
    if (!writeJsonLine(stdinPipe.wr, rateReq)) {
        cleanup();
        result.error = "write rateLimits/read failed";
        return result;
    }

    // Read JSON-RPC responses until we get id=2 or an error
    const DWORD MSG_TIMEOUT = 12000; // 12 s total timeout
    auto start = GetTickCount64();

    while (true) {
        if (GetTickCount64() - start > MSG_TIMEOUT) {
            result.error = "app-server query timed out";
            cleanup();
            return result;
        }

        bool to = false;
        std::string msg = readJsonLine(stdoutPipe.rd, MSG_TIMEOUT, to);
        if (to) {
            result.error = "app-server read timeout";
            cleanup();
            return result;
        }
        if (msg.empty()) {
            result.error = "app-server closed unexpectedly";
            cleanup();
            return result;
        }

        // Skip non-response messages (e.g. server notifications)
        if (msg.find("\"id\":") == std::string::npos) continue;

        // Look for id=2 response (rateLimits/read)
        if (msg.find("\"id\":2") != std::string::npos || msg.find("\"id\": 2") != std::string::npos) {
            result.success = true;
            result.json = std::move(msg);
            cleanup();
            return result;
        }

        // Check for errors
        if (msg.find("\"error\"") != std::string::npos) {
            result.error = msg;
            cleanup();
            return result;
        }
    }
}
