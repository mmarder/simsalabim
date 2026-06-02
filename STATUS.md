# Project Status — for new sessions

Read this first, then `DEVELOPMENT.md` (process), `ARCHITECTURE.md` (firmware
reference), and `COMMISSIONING.md` (flashing/WiFi/OTA field guide).

_Last updated: 2026-06-02 — after v0.2 release + OTA self-update verified on hardware._

---

## Where we are: Phase 0 complete and verified on hardware ✅

Phase 0 (bootstrap) is built, released, and **verified on a physical ESP32**.

| Capability | State |
|------------|-------|
| WiFi: captive AP setup + SPIFFS credential persistence | ✅ verified on hardware |
| WiFi: scan + log nearby APs on connect failure (commissioning aid) | ✅ verified |
| ElegantOTA browser upload at `/update` | ✅ built (endpoint live) |
| GitHub Releases self-update (public repo, no token) | ✅ **verified: v0.1→v0.2 OTA on hardware** |
| LCD 16×2 I²C, phase-aware screens | ✅ built (shows IP when connected) |
| AsyncWebServer + WebSocket dashboard with mock data | ✅ verified (live `/api/state`, dashboard renders) |
| Dashboard System/Update card + install button | ✅ built |
| Real sensors / control / safety logic | ⬜ Phase 1+ |

**Latest release:** `v0.5` on https://github.com/mmarder/simsalabim/releases
(repo is **public**).
**Current dev board:** running `v0.5`, joined to WiFi `Thomas`, last seen at `192.168.1.80`.

**Tests + CI (added 2026-06-02):** first host-native unit tests
(`test/test_version`, 9 cases) cover the OTA version logic (`src/ota_version.h`,
header-only pure module). GitHub Actions (`.github/workflows/ci.yml`) runs
`pio test -e native` + `pio run -e esp32dev` on every push/PR. Releases must be
on a green commit (see RELEASING.md). **This is the test harness for Phase 1** —
new control/safety logic goes into host-testable pure modules with tests.

**v0.5 adds:** **auto-deploy releases** — a release tag ending in `a` (e.g.
`v0.7a`) is installed automatically by devices (firmware + filesystem) on their
next GitHub check, no operator action. Loop guard: last auto-installed tag is
persisted in NVS so the same tag is never installed twice. `/api/ota` now also
returns `auto`. Use a plain tag (no `a`) for manual updates.

**v0.4:** **filesystem-OTA** — web UI (SPIFFS) updatable over the air.
`/api/ota/install?fs=1` and `/api/ota/install-fs` pull `spiffs.bin`. Firmware
-first/filesystem-last ordering. **Releases must include a `spiffs.bin` asset.**

**v0.3:** staged LCD boot messages; `/settings.html` hardware-status page (live)
via `/api/hardware` (OneWire DS18B20 enumeration + I²C detection + live inputs).

### What was verified on the physical board (2026-06-01/02)
- Merged image flashes at `0x0`, `Hash of data verified`.
- Chip ESP32-D0WD-V3, 4 MB flash (matches the build).
- Boots cleanly; SPIFFS mounts; no crash.
- WiFi state machine: connects to a known AP, or scans + falls back to the
  `KaeltekammerSetup` captive portal at `192.168.4.1`.
- Live dashboard data served from the device (`/api/state`).
- **GitHub OTA self-update worked end to end**: board on v0.1 detected v0.2,
  downloaded `firmware.bin` from the public release, flashed the inactive OTA
  partition, rebooted into v0.2.

**Still unverified:** anything in Phase 1 (no sensors/relays wired yet).

---

## Build environment (set up on this Mac)

- PlatformIO: `~/Library/Python/3.9/bin/pio` — `export PATH="$HOME/Library/Python/3.9/bin:$PATH"`
- Apple Silicon: **Rosetta 2 installed** (required for the x86 `mkspiffs` used by `-t buildfs`).
- `esptool`: `~/.platformio/packages/tool-esptoolpy/esptool.py`
- `gh` CLI: authenticated as **mmarder** (token in macOS keychain).
- `pyserial` installed (host-side serial capture).
- Board USB serial port on this Mac: `/dev/cu.usbserial-0001` (CH340).

### Library notes (learned the hard way)
- Web stack: `esp32async/ESPAsyncWebServer` + `esp32async/AsyncTCP`.
- ElegantOTA needs build flag `-D ELEGANTOTA_USE_ASYNC_WEBSERVER=1`.
- ⚠️ `closedcube/ClosedCube_SHT31D` does **not** exist in the registry — for
  Phase 1 humidity use `robtillaart/SHT31` (verify before relying on it).
- Phase 1+ sensor/Modbus libs are commented out in `platformio.ini`.

---

## Secrets & config

- `src/config_secrets.h` holds WiFi credentials only (GitHub token is empty —
  the repo is public, OTA needs no token). It is **git-ignored**; only
  `config_secrets.h.example` is committed. Always confirm `git status` does not
  list `config_secrets.h` before committing.
- Current baked-in WiFi: SSID `Thomas` / password `Ratnauli` (the dev-bench
  network). For Samosir, change this to the on-site network and rebuild, or set
  it via the captive portal on first boot.

---

## What's next: Phase 1 — Sensors + Safety layer

This is where the mandatory test inventory (`ARCHITECTURE.md §9`) becomes
required — every control/safety function gets host-native unit tests
(`pio test -e native`) before it touches hardware.

Suggested order:
1. `SystemState.h` + the mutex-guarded shared state.
2. `sensors/temperature.cpp` — DS18B20 read + `validate_temperature()` (+ tests).
3. `control/safety.cpp` — 1 s safety task: HP/LP/over-temp → immediate stop,
   latched alarms (+ tests).
4. Wire real `SystemState` into the WebSocket broadcast, replacing the mock
   generator in `web_server.cpp`.
5. Make the boot-time WiFi connect **non-blocking** so the control loop never
   waits on the network (currently `wifi_manager::begin()` blocks up to 60 s).

**Blocker for Modbus/compressor work:** `SS6` — the Zenith ZSR4022 Modbus
register map is still unknown (`ARCHITECTURE.md §12`). Until then, compressor
control falls back to relay K1.

---

## Docs map
- `DEVELOPMENT.md` — the mandatory 7-phase process.
- `ARCHITECTURE.md` — full firmware reference (modules, state, control logic, tests).
- `RELEASING.md` — how to cut a new release (build → merge → tag → upload).
- `COMMISSIONING.md` — flashing, WiFi setup, OTA, and troubleshooting (field guide).
