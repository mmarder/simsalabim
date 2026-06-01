# Kältekammer ESP32

Controller firmware for the ice-cream cold store on Samosir Island, Lake Toba.

See `../ARCHITECTURE.md` for the full system reference and `../DEVELOPMENT.md`
for the mandatory development process.

## Current state: Phase 0 — Bootstrap

WiFi + OTA + LCD + web dashboard mockup. No sensors or control logic yet.

| Feature | Status |
|---------|--------|
| WiFi (captive AP setup + SPIFFS persistence) | ✅ |
| ElegantOTA browser upload (`/update`) | ✅ |
| GitHub Releases OTA check + install | ✅ |
| LCD 16×2 I2C status display | ✅ |
| AsyncWebServer + WebSocket dashboard (mock data) | ✅ |
| Sensors / control / safety | Phase 1+ |

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
as a release asset. The device checks every 6 h (and on demand) and can install
the newer version. Repo is public → no token needed (`GITHUB_TOKEN` empty).
