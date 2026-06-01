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

// Triggers an immediate GitHub version check (e.g. from a dashboard button).
void checkNow();

// Downloads and flashes the latest GitHub release firmware.bin.
// Blocks; reboots on success. Returns false on failure (no reboot).
bool installLatest();

}  // namespace ota_manager
