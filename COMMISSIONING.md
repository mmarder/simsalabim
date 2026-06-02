# Commissioning & Troubleshooting

Field guide for flashing, WiFi setup, and OTA — plus every gotcha we hit during
Phase 0 bring-up. Read this before working on the physical hardware.

---

## 1. Flashing the ESP32

### From this Mac (development)
```bash
export PATH="$HOME/Library/Python/3.9/bin:$PATH"
cd Sources/kaeltekammer-esp32
ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
PORT=/dev/cu.usbserial-0001   # CH340 board

# Full flash (bootloader + partitions + firmware + dashboard). Wipes SPIFFS.
python3 "$ESPTOOL" --chip esp32 --port $PORT --baud 460800 write_flash 0x0 release/kaeltekammer-vX.Y-merged.bin

# Firmware-only (app partition). PRESERVES SPIFFS — keeps saved WiFi + dashboard.
python3 "$ESPTOOL" --chip esp32 --port $PORT --baud 460800 write_flash 0x10000 .pio/build/esp32dev/firmware.bin
```

**Full flash vs firmware-only — this matters:**
| | Offset | Touches SPIFFS? | Use when |
|--|--------|-----------------|----------|
| Merged image | `0x0` | **Yes — wipes it** | First flash; or you need to clear a saved WiFi (`/wifi.json`) |
| `firmware.bin` | `0x10000` | No | Iterating on code while keeping saved WiFi + dashboard |

### From a Windows machine
Use the merged image `kaeltekammer-vX.Y-merged.bin` (from the GitHub release) at
offset `0x0`. Full step-by-step is in each release's `FLASH_WINDOWS.txt`.

**⚠️ The "read reg error" we hit:** on Windows with the Flash Download Tool, a
`Failed to read register` / "read reg" error at connect time is almost always
the **baud rate too high**. Fixes, in order:
1. Set **BAUD = 115200** (not 921600).
2. Tick **DoNotChgBin** (the merged image already has correct flash headers).
3. Hold the **BOOT** button while clicking START (forces download mode).
The same board flashed flawlessly from the Mac at 460800 with auto-reset, so the
image was never the problem — it was the Windows cable/baud combination.

### USB driver (Windows)
The board enumerates as a COM port via CH340 (Type-C) or CP2102 (Micro-USB).
Drivers are in the repo: `Sources/MC KIT ESP32/DRIVER FOR ESP32/`.

---

## 2. WiFi setup

### Two credential sources, with precedence
On every boot `wifi_manager` loads credentials in this order:
1. **`/wifi.json` in SPIFFS** — saved via the captive portal. **Takes precedence.**
2. **Compiled-in `WIFI_SSID`/`WIFI_PASSWORD`** from `config_secrets.h` — fallback.

It then tries to connect for up to **60 s**. On success → station mode (dashboard
live). On failure → it **scans and logs nearby APs** over serial, then opens the
`KaeltekammerSetup` captive portal at `192.168.4.1`.

> **GOTCHA we hit:** a network saved through the portal (`/wifi.json`) overrides
> the compiled-in SSID **forever**, even across firmware-only reflashes (those
> don't touch SPIFFS). Symptom: you change `WIFI_SSID` in code, reflash, and the
> board *still* tries the old portal-saved network. **Fix:** do a full flash at
> `0x0` (wipes `/wifi.json`), or re-enter the network via the portal.

### Captive portal (no reflash needed)
1. Connect a phone/laptop to the **`KaeltekammerSetup`** WiFi (open network).
2. Browse to **`http://192.168.4.1`**.
3. Enter the exact SSID + password → it saves and reboots into that network.

### Gotchas with the network itself
- **2.4 GHz only.** The ESP32 cannot see or join 5 GHz networks. If a router
  (e.g. Starlink) band-steers under one SSID, make sure the 2.4 GHz radio is on.
- **SSID is case-sensitive.** `STARLINK` ≠ `Starlink`. We lost time on exactly this.
- **Use the scan log** to see what's actually reachable: on a failed connect the
  serial prints every visible AP with RSSI, e.g.:
  ```
  [WiFi] connect to 'X' failed, status=1. Scanning...
  [WiFi]    1) Dortmund   RSSI -51 dBm  ch1  enc
  [WiFi]    2) Thomas     RSSI -66 dBm  ch6  enc
  ```
  `status=1` = SSID not found. If your target SSID isn't in the list, it's out
  of range or on 5 GHz.

### Reading the board's state without a serial cable
The **LCD** shows the IP when connected (top line), or `Setup-Modus /
KaeltekammerSetup` when in AP mode. Quickest ground truth in the field.

---

## 3. Host-side serial capture (Mac)

The CH340 auto-reset over serial is **flaky** on this board — the reset pulse
doesn't always fire, and capture windows shorter than the 60 s WiFi timeout miss
the result. Reliable pattern (pyserial):
```python
import serial, time
s = serial.Serial('/dev/cu.usbserial-0001', 115200, timeout=0.5)
s.dtr = False; s.rts = True; time.sleep(0.4); s.rts = False   # reset into app
time.sleep(0.05); s.reset_input_buffer()
# then read for >70s to catch the WiFi connect/AP result
```
If serial is uncooperative, just read the LCD or hit the HTTP API instead.

---

## 4. OTA updates

### A) Local browser upload (ElegantOTA) — trusted channel
`http://<board-ip>/update` → user `admin`, password `kk-samosir` → upload a
`firmware.bin`. Works on the LAN with no internet.

### B) GitHub self-update — convenience channel (verified working)
The device checks the latest GitHub release and can self-update over WiFi.
Endpoints (GET-friendly for browser use during bring-up):
- `http://<board-ip>/api/ota` → `{current, latest, available, installing}`
- `http://<board-ip>/api/ota/check` → force a GitHub check
- `http://<board-ip>/api/ota/install` → download + flash latest, reboot

Or use the **System/Update card** on the dashboard.

**Verified flow (v0.1 → v0.2):** check → `available:true` → install → device
downloads `firmware.bin` from the public release, flashes the inactive OTA
partition, reboots into the new version. OTA updates the **app partition only**
— SPIFFS (dashboard + saved WiFi) is preserved.

> **TLS gotcha we hit:** the GitHub HTTPS check first failed with `HTTP -1`
> (TLS handshake) because a pinned DigiCert root didn't match GitHub's chain.
> Fix: the firmware uses `client.setInsecure()` (no cert validation) on purpose —
> a pinned cert would break OTA whenever GitHub rotates roots, stranding a remote
> device. See the security note in `ARCHITECTURE.md §11.1`.

**Requirements for GitHub OTA to work:**
- Board on a WiFi with **internet**.
- Repo **public** (no token), or `GITHUB_TOKEN` set in `config_secrets.h` for a
  private repo.
- A release whose tag (`vN.N`) is newer than the running `FIRMWARE_VERSION`.

---

## 5. Quick diagnostic checklist

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| Windows "read reg error" | baud too high | BAUD 115200 + DoNotChgBin + hold BOOT |
| Board ignores new compiled SSID | `/wifi.json` overrides it | full flash at `0x0` to wipe SPIFFS |
| Can't join WiFi, scan lists others | wrong case / 5 GHz / out of range | check exact SSID, enable 2.4 GHz |
| `mkspiffs ... Bad CPU type` (Mac) | x86 tool on Apple Silicon | `softwareupdate --install-rosetta` |
| OTA check `HTTP -1` | TLS/cert or no internet | confirm `setInsecure()`; confirm internet |
| OTA `available:false` after release | tag not newer than running version | bump `FIRMWARE_VERSION`; re-check |
| No serial output on Mac | flaky CH340 auto-reset | retry reset pulse, or read LCD/HTTP |
