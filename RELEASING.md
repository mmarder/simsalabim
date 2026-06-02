# Release Process

How to cut a new firmware release after code changes. Produces the same three
assets as v0.1: a merged image for first-time Windows flashing, a plain
`firmware.bin` for over-the-air self-update, and the Windows flash guide.

Prerequisites (already true on this Mac — see `STATUS.md`):
- PlatformIO on PATH: `export PATH="$HOME/Library/Python/3.9/bin:$PATH"`
- Rosetta 2 installed (for `mkspiffs` on Apple Silicon)
- `gh` authenticated as `mmarder`

For flashing details, WiFi setup, and OTA troubleshooting, see `COMMISSIONING.md`.

All commands run from the project root:
`Sources/kaeltekammer-esp32/`

---

## 1. Bump the version

Edit **one** line in `src/config.h`:

```c
#define FIRMWARE_VERSION  "v0.2"     // ← bump this
```

The version shows on the LCD, in the dashboard, and is what the in-firmware
GitHub OTA compares against the latest release tag. The git tag in step 5 must
match this string exactly (`v0.2`).

> Versioning: `vMAJOR.MINOR`. Bump MINOR for normal changes; MAJOR for breaking
> hardware/wiring or partition changes. The OTA comparator in
> `ota_manager.cpp` only understands this `vN.N` form.
>
> **Auto-deploy:** append a lowercase `a` to the tag (e.g. `v0.7a`) to make
> eligible devices install it automatically (firmware + filesystem) on their
> next GitHub check — no operator action. Set `FIRMWARE_VERSION` to match
> (e.g. `"v0.7a"`). Omit the `a` for a manual, operator-confirmed update. The
> `a` is ignored by the version comparison, so the numeric version must still
> increase. A loop guard (NVS) prevents re-installing the same auto tag.

---

## 2. Run the process gates (do not skip)

Per `DEVELOPMENT.md`, before any release:

```bash
export PATH="$HOME/Library/Python/3.9/bin:$PATH"

# Unit tests (required once Phase 1 logic exists; Phase 0 has none yet)
pio test -e native

# Clean compile, zero errors
pio run -e esp32dev

# Confirm the app still fits the OTA partition (app0/app1 = 1.75 MB each)
pio run -e esp32dev -t size
```

If this release touched the safety/control layer, complete **Phase 4 (Safety
Review)** and **Phase 6 (Test Verification)** from `DEVELOPMENT.md` first.

---

## 3. Build the binaries

```bash
export PATH="$HOME/Library/Python/3.9/bin:$PATH"

pio run -e esp32dev            # → .pio/build/esp32dev/firmware.bin
pio run -e esp32dev -t buildfs # → .pio/build/esp32dev/spiffs.bin  (web dashboard)
```

Both must succeed. `buildfs` packages everything in `data/` into the SPIFFS
image — always rebuild it if you changed any file under `data/`.

---

## 4. Merge into a single flashable image

This combines bootloader + partition table + OTA-data + firmware + dashboard
into one file that flashes at offset `0x0` (what the Windows flow needs).

```bash
VERSION=v0.2     # ← match config.h
cd .pio/build/esp32dev

ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
BOOTAPP=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
OUT=../../../release/kaeltekammer-$VERSION-merged.bin

python3 "$ESPTOOL" --chip esp32 merge_bin -o "$OUT" \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000   bootloader.bin \
  0x8000   partitions.bin \
  0xe000   "$BOOTAPP" \
  0x10000  firmware.bin \
  0x390000 spiffs.bin

cp firmware.bin ../../../release/firmware.bin
cp spiffs.bin   ../../../release/spiffs.bin
cd ../../..
```

> The offsets are fixed by `partitions_ota.csv`. If you ever change the
> partition table, update `0x390000` (the SPIFFS offset) to match.

After this, `release/` holds:
- `kaeltekammer-<VERSION>-merged.bin` — Windows flash @ `0x0`
- `firmware.bin` — firmware OTA self-update asset (app partition)
- `spiffs.bin` — **filesystem OTA asset** (web UI); required for `?fs=1` /
  `/api/ota/install-fs` to work. Always ship it.
- `FLASH_WINDOWS.txt` — keep current if the flow changed

---

## 5. Commit, tag, and publish

```bash
VERSION=v0.2

# Safety check: the secret must NOT be staged
git add -A
git status --short | grep -q "config_secrets.h$" && { echo "ABORT: secret staged"; exit 1; }

git commit -m "$VERSION: <one-line summary of what changed>"
git push

# Create the release; tag MUST equal FIRMWARE_VERSION
gh release create "$VERSION" \
  release/kaeltekammer-$VERSION-merged.bin \
  release/firmware.bin \
  release/spiffs.bin \
  release/FLASH_WINDOWS.txt \
  --repo mmarder/simsalabim \
  --title "$VERSION — <short title>" \
  --notes "<what changed; flashing reminder>"
```

Verify the assets uploaded:

```bash
gh release view "$VERSION" --repo mmarder/simsalabim \
  --json assets --jq '.assets[] | "\(.name)  \(.size)"'
```

---

## 6. After publishing

- **Windows first-flash:** download `kaeltekammer-<VERSION>-merged.bin`, flash @ `0x0`
  (see `FLASH_WINDOWS.txt`).
- **Devices already in the field:** they auto-detect the new tag within 6 h (or
  via the dashboard's update button) and pull `firmware.bin` over OTA. Because
  the repo is private, the device authenticates with the `GITHUB_TOKEN` baked
  into `config_secrets.h` — if that token has expired, OTA will fail until the
  firmware is reflashed with a fresh token.
- Update `STATUS.md` so the next session sees the new state.

---

## Quick reference — the whole thing

```bash
export PATH="$HOME/Library/Python/3.9/bin:$PATH"
VERSION=v0.2
# 1. bump FIRMWARE_VERSION in src/config.h to $VERSION
pio test -e native && pio run -e esp32dev
pio run -e esp32dev -t buildfs
# merge (step 4) ...
git add -A && git status --short | grep config_secrets.h && echo "STOP" 
git commit -m "$VERSION: ..." && git push
gh release create "$VERSION" release/*.bin release/FLASH_WINDOWS.txt \
  --repo mmarder/simsalabim --title "$VERSION" --notes "..."
```
