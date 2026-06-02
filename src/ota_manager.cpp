#include "ota_manager.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>

namespace ota_manager {

namespace {
String g_latestTag = "";
uint32_t g_lastCheck = 0;
bool g_checkRequested = false;
bool g_installFwRequested = false;
bool g_installFsRequested = false;
bool g_installing = false;

// Compare semantic-ish version tags "vMAJOR.MINOR". Returns true if `tag`
// is strictly newer than the running FIRMWARE_VERSION. Tolerant of a leading
// 'v' and missing minor component. Non-numeric → treated as not-newer.
bool isNewer(const String& tag) {
  auto parse = [](const String& s, long& maj, long& min) {
    int i = 0;
    if (i < (int)s.length() && (s[i] == 'v' || s[i] == 'V')) i++;
    maj = 0; min = 0;
    long* cur = &maj;
    bool any = false;
    for (; i < (int)s.length(); i++) {
      char c = s[i];
      if (c >= '0' && c <= '9') { *cur = (*cur) * 10 + (c - '0'); any = true; }
      else if (c == '.' && cur == &maj) { cur = &min; }
      else break;
    }
    return any;
  };
  long tMaj, tMin, cMaj, cMin;
  if (!parse(tag, tMaj, tMin)) return false;
  if (!parse(String(FIRMWARE_VERSION), cMaj, cMin)) return false;
  if (tMaj != cMaj) return tMaj > cMaj;
  return tMin > cMin;
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
    g_installFwRequested = g_installFsRequested = false;
    g_installing = true;

    // Firmware first: it goes to the INACTIVE OTA bank, so a download failure
    // here changes nothing and we abort before touching the single-bank SPIFFS.
    bool ok = true;
    if (fw) ok = downloadAndFlash("firmware.bin", U_FLASH);
    // Filesystem last (just before reboot) to minimise the window in which a
    // failed SPIFFS write could matter. SPIFFS.begin(true) auto-formats a
    // corrupt FS on next boot, so a failure here is recoverable (UI re-flash).
    if (ok && fs) ok = downloadAndFlash("spiffs.bin", U_SPIFFS);

    g_installing = false;
    if (ok && (fw || fs)) { delay(500); ESP.restart(); }
    return;
  }

  if (g_checkRequested ||
      (g_lastCheck == 0) ||
      (millis() - g_lastCheck > GITHUB_CHECK_INTERVAL_MS)) {
    g_checkRequested = false;
    g_lastCheck = millis();
    fetchLatestTag();
  }
}

String latestRelease() { return g_latestTag; }

bool updateAvailable() {
  return g_latestTag.length() > 0 && isNewer(g_latestTag);
}

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
