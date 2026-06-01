#include "web_server.h"
#include "config.h"

#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <math.h>

namespace web_server {

namespace {
AsyncWebServer g_server(WEB_PORT);
AsyncWebSocket g_ws("/ws");

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type,
               void*, uint8_t*, size_t) {
  // Phase 0: connection lifecycle only; no inbound messages handled yet.
  (void)type;
}

// Phase 0 mock: produces a plausible DEEP_COOL snapshot that drifts over time
// so the dashboard visibly updates. Replaced by real SystemState in Phase 1+.
void buildMockState(JsonDocument& doc) {
  float t = millis() / 1000.0f;
  float wob = sinf(t / 30.0f);  // slow oscillation for visible movement

  doc["ts"] = (uint32_t)(millis() / 1000);
  doc["mode"] = "DEEP_COOL";
  float tk = -29.4f + wob * 0.6f;
  doc["T_kammer_mean"] = roundf(tk * 10) / 10.0f;
  JsonArray k = doc["T_kammer"].to<JsonArray>();
  k.add(roundf((tk + 0.3f) * 10) / 10.0f);
  k.add(roundf(tk * 10) / 10.0f);
  k.add(roundf((tk - 0.3f) * 10) / 10.0f);
  doc["T_kopf"] = 68.2f + wob * 3;
  doc["T_saug"] = -19.8f;
  doc["T_ww_oben"] = 58.1f;
  doc["T_ww_unten"] = 52.3f;
  doc["T_aussen"] = 22.4f;
  doc["T_verd"] = -33.1f;
  doc["T_bphe"] = 55.7f;
  doc["T_whirlpool"] = 31.2f;
  doc["RH_kammer"] = 81.0f;
  doc["pv_w"] = (int)(8800 + wob * 400);
  doc["grid_w"] = (int)(-2100 + wob * 300);
  doc["comp_w"] = 2100;
  doc["pv_day_kwh"] = 14.2f;
  doc["pv_valid"] = true;
  doc["comp_running"] = true;
  doc["comp_on_s"] = (uint32_t)(millis() / 1000) % 7200;
  doc["blower_on"] = true;
  doc["defrost_on"] = false;
  doc["bypass_open"] = false;
  doc["evi_open"] = true;
  doc["door_open"] = false;
  doc["hp_ok"] = true;
  doc["lp_ok"] = true;
  doc["ss_current_a"] = 21.4f;
  doc["ss_valid"] = true;
  doc["alarm_flags"] = 0;
  doc["uptime_s"] = (uint32_t)(millis() / 1000);
  doc["fw"] = FIRMWARE_VERSION;
  doc["mock"] = true;  // dashboard shows a "DEMO" badge while true
}
}  // namespace

void begin() {
  // Static files from SPIFFS; "/" → index.html.
  g_server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // REST: current state snapshot (mock in Phase 0).
  g_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    buildMockState(doc);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
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
  buildMockState(doc);
  String out;
  serializeJson(doc, out);
  g_ws.textAll(out);
  g_ws.cleanupClients();
}

uint32_t clientCount() { return g_ws.count(); }

}  // namespace web_server
