# MoltbookMonitor

[![Build and Release](https://github.com/Cs1799205202/MoltbookMonitor/actions/workflows/release.yml/badge.svg)](https://github.com/Cs1799205202/MoltbookMonitor/actions/workflows/release.yml)
[![Latest Release](https://img.shields.io/github/v/release/Cs1799205202/MoltbookMonitor?sort=semver)](https://github.com/Cs1799205202/MoltbookMonitor/releases)

MoltbookMonitor is a desktop monitoring tool for tracking Moltbook agent activity. It focuses on inactivity detection for posting and replying behavior, with visual countdowns, notifications, and request diagnostics.

## What This Tool Does

- Monitors multiple Moltbook agents by `Agent ID`.
- Verifies an agent before adding it to the monitor list.
- Supports optional `Human Owner` for each agent and groups agents by human owner in the UI.
- Tracks both:
  - last post timestamp
  - last reply timestamp
- Uses `agent.posts_count` / `agent.comments_count` deltas as a fallback activity signal when `recentPosts` / `recentComments` are temporarily empty or lagging, then performs a short follow-up refresh to reconcile precise history.
- Shows whether latest post/reply activity is `Confirmed` (recent timeline) or `Inferred` (count-delta fallback) directly on each activity card.
- Raises diagnostic alerts when count regressions or abnormal count jumps are detected (for example, total counts suddenly decrease).
- Supports independent inactivity thresholds for posts and replies (in minutes).
- Supports batch import/export through Excel-friendly CSV:
  - `agent_id`
  - `post_threshold_minutes`
  - `reply_threshold_minutes`
  - `human_owner_name` (optional)
  - Import uses upsert semantics: existing agents are updated instead of skipped.
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

## Project Structure

- `src/app/main.cpp`
  - Qt application startup, system tray integration, QML engine bootstrap.
- `src/monitor/`
  - `monitorcontroller.h`: model/API boundary exposed to QML.
  - `monitorcontroller_model.cpp`: list-model roles, countdown logic, status properties.
  - `monitorcontroller_agents.cpp`: add/remove/refresh agents and snapshot merge logic.
  - `monitorcontroller_profile.cpp`: Moltbook API request, response parsing, request logging.
  - `monitorcontroller_updates.cpp`: GitHub release check, update package download/apply flow.
  - `monitorcontroller_state.cpp`: local persistence load/save and state file path handling.
- `qml/`
  - `Main.qml`: composition shell only.
  - `components/`: UI panels and delegates (`AgentCard`, `RequestLogPanel`, etc.).
  - `js/UiHelpers.js`: shared UI helper functions (countdown colors, log filtering).

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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMOLTBOOKMONITOR_VERSION_OVERRIDE=0.1.7
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix dist
```

### macOS (Release)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMOLTBOOKMONITOR_VERSION_OVERRIDE=0.1.7
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix dist
```

Qt deployment is integrated into the CMake install flow via Qt's generated deploy script. On macOS, the install step also performs an ad-hoc re-sign of the final `.app` bundle and verifies the signature to avoid Apple Silicon runtime kill (`Code Signature Invalid`).

## CI and Release

GitHub Actions workflow: `.github/workflows/release.yml`

- Build matrix:
  - `windows-2022` (x64)
  - `macos-26` (Apple Silicon arm64)
- Triggered on:
  - tags matching `v*` only
- Tag builds produce GitHub Release assets (zip archives for each platform).

Example release artifacts:

- `MoltbookMonitor-0.1.7-windows-x64.zip`
- `MoltbookMonitor-0.1.7-macos-arm64.zip`

## Versioning

- Semantic versioning is used.
- CMake default version is `0.1.7`.
- CI/release override uses `MOLTBOOKMONITOR_VERSION_OVERRIDE` to avoid stale cache drift.
- Runtime app version is generated from CMake `PROJECT_VERSION` into `app_version.h`, then consumed by both startup and monitor logic.
- Git tags use a `v` prefix (example: `v0.1.7`).

## Local Data Storage

State is saved to:

- `QStandardPaths::AppDataLocation/monitor_state.json`
- Fallback if unavailable: `~/.moltbook-monitor/monitor_state.json`

Saved content includes API key, monitored agents, thresholds, latest timestamps, and operation history.

## Security Notes

- API key is stored locally in the state file.
- Request logs mask the Authorization token for display.
- Response payload logs may still contain sensitive information from upstream API responses.

## macOS Download Note

Release zip files downloaded from browsers may be marked with quarantine attributes by macOS. If launch is blocked, run:

```bash
xattr -cr /path/to/appMoltbookMonitor.app
```

This project currently uses ad-hoc signing for compatibility and testing. It does not yet include Developer ID signing or notarization.
