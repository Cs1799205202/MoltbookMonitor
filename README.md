# MoltbookMonitor

[![Build and Release](https://github.com/Cs1799205202/MoltbookMonitor/actions/workflows/release.yml/badge.svg)](https://github.com/Cs1799205202/MoltbookMonitor/actions/workflows/release.yml)
[![Latest Release](https://img.shields.io/github/v/release/Cs1799205202/MoltbookMonitor?sort=semver)](https://github.com/Cs1799205202/MoltbookMonitor/releases)

MoltbookMonitor is a desktop monitoring tool for tracking Moltbook agent activity. It focuses on inactivity detection for posting and replying behavior, with visual countdowns, notifications, and request diagnostics.

## What This Tool Does

- Monitors multiple Moltbook agents by `Agent ID`.
- Verifies an agent before adding it to the monitor list.
- Tracks both:
  - last post timestamp
  - last reply timestamp
- Supports independent inactivity thresholds for posts and replies (in minutes).
- Updates countdown status live and highlights overdue agents.
- Raises notifications when an agent exceeds a threshold.
- Shows request/response logs with keyword + status filtering.
- Persists local state (API key, agent list, thresholds, history) across restarts.

## UI and Behavior

- Timezone display is fixed to `Asia/Shanghai (CST)` in the UI.
- Background refresh runs every 60 seconds.
- Countdown values update every 1 second.
- Inactivity alerts are de-duplicated per overdue episode (an alert is sent once until the metric returns to non-overdue).
- A system tray notification is shown when supported by the platform.

## Tech Stack

- C++23
- CMake (minimum 3.16)
- Qt 6.10
  - `Qt6::Quick`
  - `Qt6::Network`
  - `Qt6::Widgets`

## API Integration

The app fetches profile data from:

- `GET https://www.moltbook.com/api/v1/agents/profile?name=<agent_id>`

Request header:

- `Authorization: Bearer <api_key>`

## Build Locally

### Prerequisites

- Qt 6.10 Desktop SDK installed
- CMake available in PATH
- A C++ toolchain:
  - Windows: MSVC (Visual Studio Build Tools)
  - macOS: Apple Clang / Xcode Command Line Tools

### Windows (Release)

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMOLTBOOKMONITOR_VERSION=0.1.1
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix dist
```

### macOS (Release)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMOLTBOOKMONITOR_VERSION=0.1.1
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix dist
```

Qt deploy steps are integrated into the CMake install flow via Qt's generated deploy script, so runtime dependencies are bundled during `cmake --install`.

## CI and Release

GitHub Actions workflow: `.github/workflows/release.yml`

- Build matrix:
  - `windows-2022` (x64)
  - `macos-26` (Apple Silicon arm64)
- Triggered on:
  - pushes to `main`
  - pull requests to `main`
  - tags matching `v*`
- Tag builds produce GitHub Release assets (zip archives for each platform).

Example release artifacts:

- `MoltbookMonitor-0.1.1-windows-x64.zip`
- `MoltbookMonitor-0.1.1-macos-arm64.zip`

## Versioning

- Semantic versioning is used.
- CMake version is controlled by `MOLTBOOKMONITOR_VERSION`.
- Git tags use a `v` prefix (example: `v0.1.1`).

## Local Data Storage

State is saved to:

- `QStandardPaths::AppDataLocation/monitor_state.json`
- Fallback if unavailable: `~/.moltbook-monitor/monitor_state.json`

Saved content includes API key, monitored agents, thresholds, latest timestamps, and operation history.

## Security Notes

- API key is stored locally in the state file.
- Request logs mask the Authorization token for display.
- Response payload logs may still contain sensitive information from upstream API responses.
