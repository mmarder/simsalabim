#include "telemetry.h"
#include "config.h"
#include "status.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

namespace telemetry {

namespace {
constexpr const char* CONFIG_FILE = "/telemetry.json";

char g_url[192] = {0};
char g_token[160] = {0};
const char* g_source = "none";

int g_lastStatus = 0;        // 0 = never, -1 = conn/TLS error, else HTTP code
uint32_t g_lastOkMs = 0;
bool g_everOk = false;

// Load effective config: SPIFFS runtime override first, else compiled default.
void loadConfig() {
  g_url[0] = 0;
  g_token[0] = 0;
  g_source = "none";

  if (SPIFFS.exists(CONFIG_FILE)) {
    File f = SPIFFS.open(CONFIG_FILE, "r");
    if (f) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err) {
        strlcpy(g_url, doc["url"] | "", sizeof(g_url));
        strlcpy(g_token, doc["token"] | "", sizeof(g_token));
        if (strlen(g_url) > 0) { g_source = "runtime"; return; }
      }
    }
  }
  // Fall back to compile-time defaults.
  strlcpy(g_url, TELEMETRY_URL, sizeof(g_url));
  strlcpy(g_token, TELEMETRY_TOKEN, sizeof(g_token));
  if (strlen(g_url) > 0) g_source = "compiled";
}
}  // namespace

void begin() { loadConfig(); }

bool enabled() { return strlen(g_url) > 0 && strlen(g_token) > 0; }

void setConfig(const char* newUrl, const char* newToken) {
  if (!newUrl || strlen(newUrl) == 0) {
    SPIFFS.remove(CONFIG_FILE);  // clear → revert to compiled default
    loadConfig();
    return;
  }
  JsonDocument doc;
  doc["url"] = newUrl;
  doc["token"] = newToken ? newToken : "";
  File f = SPIFFS.open(CONFIG_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
  loadConfig();
}

const char* url() { return g_url; }
bool tokenSet() { return strlen(g_token) > 0; }
const char* source() { return g_source; }

bool postOnce() {
  if (!enabled() || WiFi.status() != WL_CONNECTED) return false;

  JsonDocument doc;
  status::buildJson(doc);
  doc["device"] = DEVICE_NAME;
  String body;
  serializeJson(doc, body);

  WiFiClientSecure client;
  // setInsecure(): same rationale as OTA — avoids pinning a cert that breaks on
  // rotation. The bearer token authenticates the device to the ingest endpoint.
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, g_url)) { g_lastStatus = -1; return false; }
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + g_token);
  https.setTimeout(8000);  // bounded so a stalled endpoint can't hang the task

  int code = https.POST(body);
  https.end();

  g_lastStatus = code;
  if (code >= 200 && code < 300) {
    g_lastOkMs = millis();
    g_everOk = true;
    return true;
  }
  Serial.printf("[TELE] POST -> %d\n", code);
  return false;
}

int lastStatus() { return g_lastStatus; }
uint32_t secondsSinceLastOk() {
  return g_everOk ? (millis() - g_lastOkMs) / 1000 : 0;
}

}  // namespace telemetry
