# Codex Tray Gauge

Windows system tray tool that displays Codex usage quota as a dual-ring icon.

- **Outer ring**: 7-day quota window (longer-term usage)
- **Inner ring**: 5-hour quota window (most likely to exhaust first)
- Ring fill shows remaining quota percentage
- Color: green (>=50%) → yellow (>=20%) → orange (>0%) → red (0%)

## How it works

1. Finds `codex.exe` via `CODEX_CLI_PATH` env, `%LOCALAPPDATA%\OpenAI\Codex\bin\codex.exe`, or `PATH`
2. Spawns `codex app-server --listen stdio://` as a subprocess
3. Sends a JSON-RPC `account/rateLimits/read` request
4. Reads the response and parses `rateLimitsByLimitId.codex`
5. Renders a dual-ring tray icon and a tooltip

## Privacy

See [PRIVACY.md](./PRIVACY.md) for full details.

This tool:
- Only communicates with the local Codex app-server via stdio pipe
- Does **not** read browser cookies or `~/.codex/auth.json`
- Does **not** make any network requests
- Does **not** save tokens, prompts, responses, or quota data to disk
- Does **not** upload any data

## Requirements

- Windows 10 or later
- [Codex CLI](https://github.com/openai/codex) installed and logged in (`codex login`)

## Build

Requirements: CMake 3.16+, C++17 compiler (MSVC 2019+, GCC 10+, Clang 12+).

```powershell
# Option A: With Visual Studio
cmake -B build
cmake --build build --config Release

# Option B: With MinGW-w64 + Ninja
winget install Kitware.CMake Ninja-build.Ninja
cmake -S . -B build -G "Ninja" -DCMAKE_CXX_COMPILER=g++
cmake --build build --config Release
```

Output: `build\codex-tray-gauge.exe`

## Usage

1. Launch `codex-tray-gauge.exe`
2. A Codex icon appears in your system tray
3. Hover to see current quota percentages and reset countdowns
4. Right-click for menu:
   - **Refresh now** — immediately re-read quota
   - **Copy status** — copy quota text to clipboard
   - **Exit** — close the tray tool

No console window is shown. The tool runs silently in the background.

### Auto-refresh

The tool refreshes quota automatically:
- Every 5 minutes normally
- Every 3 minutes when 5h quota drops below 30%
- Every 2 minutes when 5h quota drops below 10%
- 1 minute after a read failure
- Immediately when a quota window resets

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| Grey icon with red cross | Codex not found. Install Codex CLI and log in. |
| "Not logged in" tooltip | Run `codex login` in terminal. |
| "Read timed out" | Codex app-server may be slow. Try Refresh now. |
| "No quota data" | The response structure may have changed. Check for Codex updates. |
| Icon not updating | Internet or Codex login issue. Hover for error details. |

### Checking your Codex setup

```powershell
# Is Codex installed?
where codex

# Can you log in?
codex login

# Does app-server start? (Press Ctrl+C to stop)
codex app-server --listen stdio://
```

## Known limitations

- If Codex changes its app-server response format, this tool will need an update
- Does not support multiple Codex accounts
- Quota data is not persisted between restarts
- The icon text is minimal — hover for detailed numbers

## License

MIT
