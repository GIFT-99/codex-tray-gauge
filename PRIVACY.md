# Privacy — Codex Tray Gauge

## Summary

This tool reads your Codex usage quota and displays it in the Windows system tray.
**No data is sent anywhere.**

## What this tool does

- Finds `codex.exe` on your local machine
- Spawns `codex app-server --listen stdio://` as a child process
- Sends a local JSON-RPC request to read your quota (`account/rateLimits/read`)
- Displays the remaining quota as a dual-ring tray icon

## What this tool does NOT do

- Does **not** read browser cookies (Chrome, Edge, Firefox, etc.)
- Does **not** read `~/.codex/auth.json` or any Codex configuration files
- Does **not** scan session files, token files, or credential stores
- Does **not** make any HTTP requests or network connections
- Does **not** send any data off your machine
- Does **not** save tokens, prompts, responses, or conversation history
- Does **not** write quota data, cached responses, or logs to disk
- Does **not** read files outside of the locations listed above

## Data flow

```
[Your Machine Only]
  codex-tray-gauge.exe
      ↓ stdio pipe
  codex app-server (local subprocess)
      ↓ (internal to Codex CLI)
  Codex backend (official OpenAI API)
```

The tray tool only communicates with the local Codex app-server process
via a standard input/output pipe. It never connects to the internet on its own.

## Data retention

- Quota data is held **in memory only**
- When you exit the tool, all quota data is discarded
- No log files are written to disk
- No configuration files are created (except optional Start with Windows registry entry)

## Permissions

This tool runs as a standard user application. It does not require
administrator privileges.

## Third-party code

- [nlohmann/json](https://github.com/nlohmann/json) (MIT) — JSON parsing, embedded at build time, no network access

## Questions

If you have privacy concerns, review the source code and build from source.
The entire codebase is less than 1500 lines of C++.
