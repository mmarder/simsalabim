#pragma once
// ota_manager — two OTA paths:
//   A) ElegantOTA browser upload at /update (always available on local WiFi)
//   B) GitHub Releases check + download (needs internet)
// See ARCHITECTURE.md §6, spec §0.2.
#include <Arduino.h>

class AsyncWebServer;

namespace ota_manager {

// Registers the ElegantOTA handler on the given server (mounts /update).
void begin(AsyncWebServer& server);

// Call from a low-priority task. Drives ElegantOTA's async loop and, every
// GITHUB_CHECK_INTERVAL_MS, checks GitHub for a newer release tag.
void loop();

// Latest tag found on GitHub ("" if none / not yet checked).
String latestRelease();

// True if latestRelease() is newer than FIRMWARE_VERSION.
bool updateAvailable();

// True if the latest release is an auto-deploy tag (ends in 'a', e.g. "v0.7a").
// Such releases install automatically on eligible devices.
bool latestIsAuto();

// Triggers an immediate GitHub version check (e.g. from a dashboard button).
void checkNow();

// Requests a GitHub self-update. Non-blocking: sets a flag that loop()
// (running in the web task) acts on, so the long download+flash never runs
// inside an async HTTP handler. No-op if no update is available.
//   withFilesystem=false → flash firmware.bin only (app partition; dual-bank, safe).
//   withFilesystem=true  → also flash spiffs.bin (filesystem/web UI).
// Firmware is flashed first (to the inactive bank), then the filesystem last,
// then the device reboots — so a firmware-download failure touches nothing, and
// only a rare filesystem-write failure can leave the (single-bank) SPIFFS to be
// re-flashed. See ARCHITECTURE.md §11.4 OTA caveat.
void requestInstall(bool withFilesystem = false);

// Requests a filesystem-only update (spiffs.bin → SPIFFS), then reboot.
void requestInstallFilesystem();

// True while a requested install is pending or in progress (for UI).
bool installInProgress();

}  // namespace ota_manager
