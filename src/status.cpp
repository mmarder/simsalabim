#include "status.h"
#include "config.h"
#include <math.h>
#include <WiFi.h>
#include <esp_system.h>

namespace status {

namespace {
// Human-readable reset reason — real device data, valuable for remote debugging
// (tells you WHY it last rebooted: crash, watchdog, brownout, OTA, power).
const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_SW:       return "SW (restart/OTA)";
    case ESP_RST_PANIC:    return "PANIC (crash)";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_EXT:      return "EXT";
    default:               return "OTHER";
  }
}
}  // namespace

// Phase 0 mock: a plausible DEEP_COOL snapshot that drifts over time so the
// dashboard visibly updates. Replaced by real SystemState reads in Phase 1+.
void buildJson(JsonDocument& doc) {
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

  // ── Device health: REAL data even in Phase 0 (not mock) ──────────────────
  // The frontend marks the sensor block above as demo (mock=true) but treats
  // these as live — they come straight from the running ESP32.
  doc["uptime_s"]     = (uint32_t)(millis() / 1000);
  doc["free_heap"]    = (uint32_t)ESP.getFreeHeap();
  doc["rssi"]         = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["reset_reason"] = resetReasonStr();
  doc["fw"] = FIRMWARE_VERSION;

  doc["mock"] = true;  // sensor values above are simulated (Phase 0)
}

}  // namespace status
