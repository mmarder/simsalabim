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

// DigiCert Global Root G2 — chains api.github.com and objects.githubusercontent.com.
// Used so the HTTPS cert is validated rather than blindly accepted.
const char* GITHUB_ROOT_CA = R"CERT(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----)CERT";

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
  client.setCACert(GITHUB_ROOT_CA);

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
  if (code == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, https.getStream());
    if (!err) {
      g_latestTag = String((const char*)(doc["tag_name"] | ""));
    }
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

bool installLatest() {
  if (!updateAvailable() || WiFi.status() != WL_CONNECTED) return false;

  // Resolve the firmware.bin asset URL from the release.
  WiFiClientSecure client;
  client.setCACert(GITHUB_ROOT_CA);
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
