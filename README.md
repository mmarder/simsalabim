# Kältekammer ESP32

Controller firmware for the ice-cream cold store on Samosir Island, Lake Toba.

## Documentation map
- **[STATUS.md](STATUS.md)** — start here: current state, what's verified, what's next.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — full firmware reference (modules, state, control logic, API, tests).
- **[DEVELOPMENT.md](DEVELOPMENT.md)** — the mandatory 7-phase, safety-focused process.
- **[COMMISSIONING.md](COMMISSIONING.md)** — flashing, WiFi setup, OTA, and troubleshooting (field guide).
- **[RELEASING.md](RELEASING.md)** — how to cut a new release (build → merge → tag → upload).

## Current state: Phase 0 complete — verified on hardware

Bootstrap (network, OTA, LCD, web UI, hardware bring-up) is done and tested on a
physical ESP32. No control/safety logic yet — that is Phase 1.

| Feature | Status |
|---------|--------|
| WiFi: captive AP setup + SPIFFS persistence + scan diagnostic | ✅ verified |
| LCD 16×2 I²C, staged boot messages | ✅ |
| AsyncWebServer + WebSocket dashboard (mock data) | ✅ verified |
| Hardware-status page `/settings.html` (OneWire/I²C detect, live inputs) | ✅ verified |
| ElegantOTA browser upload (`/update`) | ✅ |
| GitHub firmware OTA self-update | ✅ verified (v0.1→v0.2) |
| Filesystem OTA (web UI updatable over the air) | ✅ verified |
| Auto-deploy releases (tag ending in `a`, e.g. `v0.7a`) | ✅ verified (v0.5→v0.6a) |
| Sensors / compressor control / safety layer | ⬜ Phase 1+ |

Latest release: see https://github.com/mmarder/simsalabim/releases (repo is public).

## First-time setup

1. Install PlatformIO (`pip3 install platformio`, or the VS Code extension).
2. Copy the secrets template and fill it in:
   ```
   cp src/config_secrets.h.example src/config_secrets.h
   # edit WIFI_SSID / WIFI_PASSWORD
   ```
   `config_secrets.h` is git-ignored — it is never committed.
3. Verify the LCD I2C address (`0x27` or `0x3F`) and set `LCD_I2C_ADDR` in
   `config.h` if needed.

## Build & flash

```bash
# Compile
pio run -e esp32dev

# Upload firmware over USB
pio run -e esp32dev -t upload

# Upload the web assets (data/) to SPIFFS — required for the dashboard
pio run -e esp32dev -t uploadfs

# Serial monitor
pio device monitor

# Check binary fits the OTA partition (1.75 MB)
pio run -e esp32dev -t size
```

After the first flash, the device:
- tries the configured WiFi; on failure opens the **`KaeltekammerSetup`** AP
  (connect, browse to `192.168.4.1`, enter WiFi credentials → reboots).
- serves the dashboard at `http://<device-ip>/`.
- exposes OTA at `http://<device-ip>/update` (user `admin`, password `kk-samosir`).

## Tests

Pure logic runs on the host (no hardware) once Phase 1 modules land:

```bash
pio test -e native
```

The Phase 0 modules are I/O bootstrap and have no host-testable pure logic yet.
The mandatory test inventory for control logic is in `../ARCHITECTURE.md §9`.

## GitHub Releases OTA

Tag a release on `github.com/mmarder/simsalabim` and attach `firmware.bin`
(app), `spiffs.bin` (web UI), and `kaeltekammer-<ver>-merged.bin` (first flash).
The device checks every 6 h (and on demand) and can install the newer version.
Repo is public → no token needed (`GITHUB_TOKEN` empty).

- **Firmware OTA** updates the app partition (dual-bank, safe).
- **Filesystem OTA** (`?fs=1` / `/api/ota/install-fs`) updates the web UI from
  `spiffs.bin`.
- **Auto-deploy:** a tag ending in lowercase `a` (e.g. `v0.7a`) installs
  automatically on devices (firmware + filesystem), no operator action; a loop
  guard (NVS) prevents re-installing the same tag. Plain tag = manual update.

See [RELEASING.md](RELEASING.md) for the full procedure and [COMMISSIONING.md](COMMISSIONING.md)
for OTA details and the firmware-only-vs-full-flash rules.
