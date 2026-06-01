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
bool g_installRequested = false;
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

}  // namespace

void begin(AsyncWebServer& server) {
  ElegantOTA.setAuth(WEB_AUTH_USER, OTA_PASSWORD);
  ElegantOTA.begin(&server);
}

void loop() {
  ElegantOTA.loop();

  // A requested install takes priority. installLatest() blocks (download +
  // flash) and reboots on success; running it here keeps it out of the async
  // HTTP handler. If it returns, the install failed — clear the flag.
  if (g_installRequested) {
    g_installRequested = false;
    g_installing = true;
    installLatest();      // reboots on success; returns only on failure
    g_installing = false;
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

void requestInstall() {
  if (updateAvailable()) g_installRequested = true;
}

bool installInProgress() { return g_installing || g_installRequested; }

bool installLatest() {
  if (!updateAvailable() || WiFi.status() != WL_CONNECTED) return false;

  // Resolve the firmware.bin asset URL from the release.
  WiFiClientSecure client;
  // Skip cert validation: survives GitHub root-cert rotation (a pinned cert
  // would brick remote OTA on rotation). Firmware is public; local
  // ElegantOTA stays the trusted channel. See ARCHITECTURE.md OTA notes.
  client.setInsecure();
  HTTPClient https;
  String url = "https://github.com/" + String(GITHUB_USER) + "/" +
               String(GITHUB_REPO) + "/releases/download/" + g_latestTag +
               "/firmware.bin";

  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, url)) return false;
  https.addHeader("User-Agent", DEVICE_NAME);
  if (strlen(GITHUB_TOKEN) > 0) {
    https.addHeader("Authorization", String("Bearer ") + GITHUB_TOKEN);
  }

  int code = https.GET();
  if (code != HTTP_CODE_OK) { https.end(); return false; }

  int len = https.getSize();
  if (len <= 0 || !Update.begin(len)) { https.end(); return false; }

  size_t written = Update.writeStream(https.getStream());
  https.end();

  if (written != (size_t)len || !Update.end(true) || !Update.isFinished()) {
    return false;
  }
  delay(500);
  ESP.restart();
  return true;  // not reached
}

}  // namespace ota_manager
