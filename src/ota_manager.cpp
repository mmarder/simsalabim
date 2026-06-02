#include "ota_manager.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "ota_version.h"

namespace ota_manager {

namespace {
String g_latestTag = "";
uint32_t g_lastCheck = 0;
bool g_checkRequested = false;
bool g_installFwRequested = false;
bool g_installFsRequested = false;
bool g_installing = false;
bool g_autoInstall = false;     // set when the current install was auto-triggered

// A release tag ending in 'a' (e.g. "v0.7a") is an AUTO-DEPLOY release: eligible
// devices install it without user interaction. The 'a' is ignored by isNewer().
// Logic lives in ota_version.h (host-tested; see test/test_version).
bool isAutoTag(const String& tag) {
  return ota_version::is_auto_tag(tag.c_str());
}

// Persisted (NVS) tag of the last successfully auto-installed release. Used as a
// loop guard so a device never auto-installs the same tag twice — protects
// against a boot loop even if an auto build reports a wrong version string.
String savedAutoTag() {
  Preferences p;
  p.begin("ota", true);
  String t = p.getString("auto_tag", "");
  p.end();
  return t;
}
void saveAutoTag(const String& tag) {
  Preferences p;
  p.begin("ota", false);
  p.putString("auto_tag", tag);
  p.end();
}

// Compare semantic-ish version tags "vMAJOR.MINOR". Returns true if `tag`
// is strictly newer than the running FIRMWARE_VERSION. Tolerant of a leading
// 'v' and missing minor component. Non-numeric → treated as not-newer.
// Is `tag` newer than the running firmware? Delegates to the host-tested
// pure logic in ota_version.h (see test/test_version).
bool isNewer(const String& tag) {
  return ota_version::is_newer(tag.c_str(), FIRMWARE_VERSION);
}

// Queries GitHub for the latest release tag. Stores it in g_latestTag.
void fetchLatestTag() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  // Skip cert validation: survives GitHub root-cert rotation (a pinned cert
  // would brick remote OTA on rotation). Firmware is public; local
  // ElegantOTA stays the trusted channel. See ARCHITECTURE.md OTA notes.
  client.setInsecure();

  HTTPClient https;
  String url = "https://api.github.com/repos/" + String(GITHUB_USER) + "/" +
               String(GITHUB_REPO) + "/releases/latest";
  if (!https.begin(client, url)) return;

  https.addHeader("User-Agent", DEVICE_NAME);
  https.addHeader("Accept", "application/vnd.github+json");
  if (strlen(GITHUB_TOKEN) > 0) {
    https.addHeader("Authorization", String("Bearer ") + GITHUB_TOKEN);
  }

  int code = https.GET();
  Serial.printf("[OTA] GET %s -> HTTP %d\n", url.c_str(), code);
  if (code == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, https.getStream());
    if (!err) {
      g_latestTag = String((const char*)(doc["tag_name"] | ""));
      Serial.printf("[OTA] latest tag = '%s'\n", g_latestTag.c_str());
    } else {
      Serial.printf("[OTA] JSON parse error: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[OTA] fetch failed (negative = connection/TLS error)\n");
  }
  https.end();
}

// Downloads a named release asset and flashes it to the given Update target
// (U_FLASH = app partition, U_SPIFFS = filesystem). Blocks. Does NOT reboot —
// the caller reboots once after all requested partitions are written.
// Returns true on a fully verified write.
bool downloadAndFlash(const char* asset, int command) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  // See fetchLatestTag(): setInsecure() on purpose (GitHub cert rotation).
  client.setInsecure();
  HTTPClient https;
  String url = "https://github.com/" + String(GITHUB_USER) + "/" +
               String(GITHUB_REPO) + "/releases/download/" + g_latestTag + "/" +
               asset;

  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, url)) return false;
  https.addHeader("User-Agent", DEVICE_NAME);
  if (strlen(GITHUB_TOKEN) > 0) {
    https.addHeader("Authorization", String("Bearer ") + GITHUB_TOKEN);
  }

  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] %s download failed: HTTP %d\n", asset, code);
    https.end();
    return false;
  }

  int len = https.getSize();
  if (len <= 0 || !Update.begin(len, command)) {
    Serial.printf("[OTA] %s: bad size %d or Update.begin failed (%s)\n",
                  asset, len, Update.errorString());
    https.end();
    return false;
  }

  size_t written = Update.writeStream(https.getStream());
  https.end();

  if (written != (size_t)len || !Update.end(true) || !Update.isFinished()) {
    Serial.printf("[OTA] %s flash failed: %s\n", asset, Update.errorString());
    return false;
  }
  Serial.printf("[OTA] %s flashed OK (%u bytes)\n", asset, (unsigned)written);
  return true;
}

}  // namespace

void begin(AsyncWebServer& server) {
  ElegantOTA.setAuth(WEB_AUTH_USER, OTA_PASSWORD);
  ElegantOTA.begin(&server);
}

void loop() {
  ElegantOTA.loop();

  // A requested install takes priority. The download+flash blocks; running it
  // here (web task) keeps it out of the async HTTP handler.
  if (g_installFwRequested || g_installFsRequested) {
    bool fw = g_installFwRequested, fs = g_installFsRequested;
    bool autoMode = g_autoInstall;
    g_installFwRequested = g_installFsRequested = g_autoInstall = false;
    g_installing = true;

    // Firmware first: it goes to the INACTIVE OTA bank, so a download failure
    // here changes nothing — we abort without touching the single-bank SPIFFS.
    bool fwOk = fw ? downloadAndFlash("firmware.bin", U_FLASH) : true;
    // Filesystem last and best-effort: SPIFFS.begin(true) auto-formats a corrupt
    // FS on next boot, so a failure here is recoverable and must not block a
    // firmware update (e.g. a release with no spiffs.bin asset).
    bool fsOk = (fwOk && fs) ? downloadAndFlash("spiffs.bin", U_SPIFFS) : !fs;

    g_installing = false;
    bool didFlash = (fw && fwOk) || (fs && fsOk);
    if (didFlash) {
      // Record the auto-installed tag BEFORE rebooting so the device never
      // auto-installs it again (loop guard).
      if (autoMode && fwOk) saveAutoTag(g_latestTag);
      delay(500);
      ESP.restart();
    }
    return;
  }

  if (g_checkRequested ||
      (g_lastCheck == 0) ||
      (millis() - g_lastCheck > GITHUB_CHECK_INTERVAL_MS)) {
    g_checkRequested = false;
    g_lastCheck = millis();
    fetchLatestTag();

    // Auto-deploy: an 'a'-suffixed release that is newer and not already
    // auto-installed triggers a hands-off firmware+filesystem update.
    if (updateAvailable() && isAutoTag(g_latestTag) &&
        g_latestTag != savedAutoTag()) {
      Serial.printf("[OTA] auto-deploy tag %s — installing\n",
                    g_latestTag.c_str());
      g_autoInstall = true;
      g_installFwRequested = true;
      g_installFsRequested = true;   // keep the web UI in sync with firmware
    }
  }
}

String latestRelease() { return g_latestTag; }

bool updateAvailable() {
  return g_latestTag.length() > 0 && isNewer(g_latestTag);
}

bool latestIsAuto() { return isAutoTag(g_latestTag); }

void checkNow() { g_checkRequested = true; }

void requestInstall(bool withFilesystem) {
  if (updateAvailable()) {
    g_installFwRequested = true;
    g_installFsRequested = withFilesystem;
  }
}

void requestInstallFilesystem() {
  // Filesystem can be (re)flashed from the latest release regardless of the
  // firmware version comparison — useful for pushing a UI-only fix.
  if (g_latestTag.length() > 0) g_installFsRequested = true;
}

bool installInProgress() {
  return g_installing || g_installFwRequested || g_installFsRequested;
}

}  // namespace ota_manager
