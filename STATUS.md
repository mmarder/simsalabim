# Project Status — for new sessions

Read this first, then `DEVELOPMENT.md` (process) and `ARCHITECTURE.md` (firmware reference).
This file is the quick handoff: where we are, what works, what's next.

_Last updated: 2026-06-01 — after v0.1 release._

---

## Where we are: Phase 0 complete ✅

Phase 0 (bootstrap) is built, compiles clean, and is released as **v0.1**.

| Capability | State |
|------------|-------|
| WiFi: captive AP setup (`KaeltekammerSetup`) + SPIFFS credential persistence | ✅ built |
| ElegantOTA browser upload at `/update` | ✅ built |
| GitHub Releases self-update (private repo, token-authed) | ✅ built |
| LCD 16×2 I²C, phase-aware screens | ✅ built |
| AsyncWebServer + WebSocket dashboard with **mock** data | ✅ built |
| Real sensors / control / safety logic | ⬜ Phase 1+ |

**Compiles:** `pio run -e esp32dev` → SUCCESS (Flash 61.3%, RAM 14.9%).
**Released:** v0.1 on https://github.com/mmarder/simsalabim/releases/tag/v0.1
(assets: `kaeltekammer-v0.1-merged.bin` for Windows flash @ `0x0`, `firmware.bin` for OTA, `FLASH_WINDOWS.txt`).

**Not yet done:** the dashboard has not been verified rendering in a browser; no hardware bench test yet (no ESP32 on this machine).

---

## Build environment (already set up on this Mac)

- PlatformIO: `~/Library/Python/3.9/bin/pio` — add to PATH:
  `export PATH="$HOME/Library/Python/3.9/bin:$PATH"`
- Apple Silicon: **Rosetta 2 is installed** (required for the x86 `mkspiffs` used by `-t buildfs`).
- `gh` CLI: authenticated as **mmarder** (token in macOS keychain).
- Toolchain + libraries already downloaded — rebuilds are fast (~20 s).

### Library notes (learned the hard way)
- Web stack: `esp32async/ESPAsyncWebServer` + `esp32async/AsyncTCP` (the actively-maintained forks).
- ElegantOTA needs build flag `-D ELEGANTOTA_USE_ASYNC_WEBSERVER=1`.
- ⚠️ `closedcube/ClosedCube_SHT31D` does **not** exist in the PlatformIO registry — for Phase 1 humidity use `robtillaart/SHT31` (verify before relying on it).
- Phase 1+ sensor/Modbus libs are commented out in `platformio.ini` — uncomment/add as those modules land.

---

## Secrets

- `src/config_secrets.h` holds WiFi credentials **and** the GitHub PAT (private-repo OTA needs it).
- It is git-ignored. Only `config_secrets.h.example` is committed. **Never commit the real file.**
- Always confirm with `git status` that `config_secrets.h` is absent before any commit.

---

## What's next: Phase 1 — Sensors + Safety layer

This is where the mandatory test inventory (`ARCHITECTURE.md §9`) becomes required — every control/safety function gets host-native unit tests (`pio test -e native`) before it touches hardware.

Suggested order:
1. `SystemState.h` + the mutex-guarded shared state (the spine everything reads/writes).
2. `sensors/temperature.cpp` — DS18B20 read + `validate_temperature()` (with tests).
3. `control/safety.cpp` — the 1 s safety task: HP/LP/over-temp → immediate compressor stop, latched alarms (with tests).
4. Wire the real `SystemState` into the WebSocket broadcast, replacing the mock generator in `web_server.cpp`.

**Blocker for the Modbus/compressor work:** `SS6` — the Zenith ZSR4022 Modbus register map is still unknown (see `ARCHITECTURE.md §12`). Until then, compressor control falls back to relay K1.

---

## How to make a new release

See `RELEASING.md` for the exact step-by-step (build → merge → tag → upload).
