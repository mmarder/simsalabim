#include "web_server.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <math.h>

#include "ota_manager.h"
#include "hardware.h"
#include "status.h"
#include "telemetry.h"

namespace web_server {

namespace {
AsyncWebServer g_server(WEB_PORT);
AsyncWebSocket g_ws("/ws");

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type,
               void*, uint8_t*, size_t) {
  // Phase 0: connection lifecycle only; no inbound messages handled yet.
  (void)type;
}
}  // namespace

void begin() {
  // Static files from SPIFFS; "/" → index.html.
  g_server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // REST: current state snapshot (mock in Phase 0).
  g_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    status::buildJson(doc);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // NOTE: ESPAsyncWebServer matches a handler whose URI is a *prefix* of the
  // request path, so specific /api/ota/* and /api/hardware/* routes MUST be
  // registered BEFORE their generic parents (/api/ota, /api/hardware) — else a
  // GET to a subpath is shadowed by the parent. Order below is deliberate.

  // Force an immediate GitHub version check. GET|POST so it can be triggered
  // straight from a browser address bar during bring-up.
  g_server.on("/api/ota/check", HTTP_GET | HTTP_POST,
              [](AsyncWebServerRequest* req) {
    ota_manager::checkNow();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Request a self-update from the latest GitHub release. The actual
  // download+flash happens in the web task (see ota_manager::loop); the device
  // reboots into the new firmware on success. GET|POST for browser convenience.
  g_server.on("/api/ota/install", HTTP_GET | HTTP_POST,
              [](AsyncWebServerRequest* req) {
    if (!ota_manager::updateAvailable()) {
      req->send(409, "application/json",
                "{\"ok\":false,\"error\":\"no update available\"}");
      return;
    }
    // ?fs=1 → also update the filesystem (web UI) alongside the firmware.
    bool withFs = req->hasParam("fs") && req->getParam("fs")->value() == "1";
    ota_manager::requestInstall(withFs);
    req->send(200, "application/json", "{\"ok\":true,\"status\":\"installing\"}");
  });

  // Filesystem-only update (spiffs.bin → SPIFFS) from the latest release.
  // Updates the web UI without changing firmware. Reboots on success.
  g_server.on("/api/ota/install-fs", HTTP_GET | HTTP_POST,
              [](AsyncWebServerRequest* req) {
    ota_manager::requestInstallFilesystem();
    req->send(200, "application/json", "{\"ok\":true,\"status\":\"installing\"}");
  });

  // OTA status (generic parent — AFTER the /api/ota/* subpaths above).
  g_server.on("/api/ota", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["current"]   = FIRMWARE_VERSION;
    doc["latest"]    = ota_manager::latestRelease();
    doc["available"] = ota_manager::updateAvailable();
    doc["installing"] = ota_manager::installInProgress();
    doc["auto"]      = ota_manager::latestIsAuto();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Re-scan the I2C/OneWire buses (handled by the LCD task to avoid bus races).
  g_server.on("/api/hardware/rescan", HTTP_GET | HTTP_POST,
              [](AsyncWebServerRequest* req) {
    hardware::requestRescan();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Hardware status (generic parent — AFTER /api/hardware/* subpaths above).
  g_server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    hardware::toJson(doc);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Telemetry config: set URL+token (POST) or view status (GET). The token is
  // never returned. Lets an operator point the device at the cloud endpoint
  // from the settings page (stored in SPIFFS, overrides the compiled default).
  g_server.on("/api/telemetry", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["enabled"]   = telemetry::enabled();
    doc["url"]       = telemetry::url();
    doc["token_set"] = telemetry::tokenSet();
    doc["source"]    = telemetry::source();
    doc["last_status"] = telemetry::lastStatus();
    doc["secs_since_ok"] = telemetry::secondsSinceLastOk();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });
  g_server.on("/api/telemetry", HTTP_POST,
              [](AsyncWebServerRequest* req) {
    String url = req->hasParam("url", true) ? req->getParam("url", true)->value() : "";
    String token = req->hasParam("token", true) ? req->getParam("token", true)->value() : "";
    telemetry::setConfig(url.c_str(), token.c_str());
    req->send(200, "application/json", "{\"ok\":true}");
  });

  g_server.addHandler(&g_ws);
  g_ws.onEvent(onWsEvent);

  g_server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });
}

void start() { g_server.begin(); }

AsyncWebServer& server() { return g_server; }

void broadcastState() {
  if (g_ws.count() == 0) return;
  JsonDocument doc;
  status::buildJson(doc);
  String out;
  serializeJson(doc, out);
  g_ws.textAll(out);
  g_ws.cleanupClients();
}

uint32_t clientCount() { return g_ws.count(); }

}  // namespace web_server
