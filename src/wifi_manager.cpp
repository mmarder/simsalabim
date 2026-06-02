#include "wifi_manager.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

namespace wifi_manager {

namespace {
constexpr byte DNS_PORT = 53;
DNSServer dnsServer;
bool apMode = false;
bool credentialsLoaded = false;
char ssid[64] = {0};
char pass[64] = {0};
uint32_t lastReconnectAttempt = 0;

// Load /wifi.json from SPIFFS into ssid/pass. Falls back to the compiled-in
// WIFI_SSID/WIFI_PASSWORD if the file is missing (first flash convenience).
bool loadCredentials() {
  if (SPIFFS.exists(WIFI_CONFIG_FILE)) {
    File f = SPIFFS.open(WIFI_CONFIG_FILE, "r");
    if (f) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err) {
        strlcpy(ssid, doc["ssid"] | "", sizeof(ssid));
        strlcpy(pass, doc["password"] | "", sizeof(pass));
        if (strlen(ssid) > 0) return true;
      }
    }
  }
  // Fallback: compiled-in dev credentials.
  strlcpy(ssid, WIFI_SSID, sizeof(ssid));
  strlcpy(pass, WIFI_PASSWORD, sizeof(pass));
  return strlen(ssid) > 0;
}

bool saveCredentials(const char* newSsid, const char* newPass) {
  JsonDocument doc;
  doc["ssid"] = newSsid;
  doc["password"] = newPass;
  File f = SPIFFS.open(WIFI_CONFIG_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

// Minimal captive portal: a single form that POSTs SSID/password, saves to
// SPIFFS, then reboots into station mode. Served by a tiny synchronous server
// so it has no dependency on the main AsyncWebServer (which only runs once
// connected). Implemented in startAccessPoint().
WebServer* portal = nullptr;

void handlePortalRoot();
void handlePortalSave();

void startAccessPoint() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", apIP);

  portal = new WebServer(80);
  portal->on("/", handlePortalRoot);
  portal->on("/save", HTTP_POST, handlePortalSave);
  portal->onNotFound(handlePortalRoot);  // captive: redirect everything to form
  portal->begin();
}

void handlePortalRoot() {
  String html =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<title>" DEVICE_NAME " Setup</title></head><body "
    "style='font-family:sans-serif;max-width:400px;margin:2em auto;padding:1em'>"
    "<h2>&#10052; K&auml;ltekammer Setup</h2>"
    "<form method='POST' action='/save'>"
    "<p>WiFi SSID:<br><input name='ssid' style='width:100%;padding:.5em'></p>"
    "<p>Passwort:<br><input name='password' type='password' "
    "style='width:100%;padding:.5em'></p>"
    "<button type='submit' style='padding:.7em 1.5em'>Speichern &amp; "
    "Neustart</button></form></body></html>";
  portal->send(200, "text/html", html);
}

void handlePortalSave() {
  String s = portal->arg("ssid");
  String p = portal->arg("password");
  if (s.length() > 0) {
    saveCredentials(s.c_str(), p.c_str());
    portal->send(200, "text/html",
      "<html><body style='font-family:sans-serif;text-align:center;margin-top:3em'>"
      "<h3>Gespeichert. Neustart&hellip;</h3></body></html>");
    delay(1500);
    ESP.restart();
  } else {
    portal->send(400, "text/html", "SSID darf nicht leer sein.");
  }
}

}  // namespace

const char* currentSsid() { return ssid; }

bool begin(ProgressFn progress) {
  if (!credentialsLoaded) {
    loadCredentials();
    credentialsLoaded = true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.begin(ssid, pass);

  const int max_s = WIFI_CONNECT_TIMEOUT_MS / 1000;
  uint32_t start = millis();
  int lastReported = -1;
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    int elapsed = (millis() - start) / 1000;
    if (progress && elapsed != lastReported) {
      lastReported = elapsed;
      progress(ssid, elapsed, max_s);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    return true;
  }

  // Connection failed → log why, then scan so we can see what IS reachable.
  Serial.printf("[WiFi] connect to '%s' failed, status=%d. Scanning...\n",
                ssid, (int)WiFi.status());
  // Reset the radio to a clean STA state so the scan is reliable.
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(200);
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("[WiFi] scan found no networks");
  } else {
    for (int i = 0; i < n; i++) {
      Serial.printf("[WiFi]   %2d) %-28s  RSSI %d dBm  ch%d  %s\n",
                    i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "enc");
    }
  }

  // Captive AP for reconfiguration.
  startAccessPoint();
  return false;
}

bool isConnected() {
  return !apMode && WiFi.status() == WL_CONNECTED;
}

String ipAddress() {
  return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

int rssi() {
  return apMode ? 0 : WiFi.RSSI();
}

void loop() {
  if (apMode) {
    dnsServer.processNextRequest();
    if (portal) portal->handleClient();
    return;
  }

  // Station mode: attempt a reconnect every 30 s if the link dropped.
  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnectAttempt > 30000) {
    lastReconnectAttempt = millis();
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
  }
}

}  // namespace wifi_manager
