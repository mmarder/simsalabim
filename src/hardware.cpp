#include "hardware.h"
#include "config.h"

#include <Wire.h>
#include <OneWire.h>
#include <ArduinoJson.h>

namespace hardware {

namespace {
OneWire ow(PIN_ONEWIRE);

struct Scan {
  uint8_t i2c_addr[16];
  int     i2c_count = 0;
  int     onewire_total = 0;     // all OneWire ROMs found
  int     ds18b20_count = 0;     // family code 0x28
  bool    lcd_present = false;   // 0x27 or 0x3F
  bool    sht31_present = false; // 0x44 or 0x45
  uint32_t last_scan_ms = 0;
  bool    done = false;
};
Scan g_scan;
volatile bool g_rescanRequested = false;

// Probe one I2C address.
bool i2cAck(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void doScan() {
  Scan s;

  // ── I2C bus (shared with the LCD on GPIO13/14) ──────────────
  for (uint8_t a = 1; a < 127; a++) {
    if (i2cAck(a)) {
      if (s.i2c_count < (int)(sizeof(s.i2c_addr))) s.i2c_addr[s.i2c_count++] = a;
      if (a == 0x27 || a == 0x3F) s.lcd_present = true;
      if (a == 0x44 || a == 0x45) s.sht31_present = true;
    }
  }

  // ── OneWire bus (GPIO4) ─────────────────────────────────────
  uint8_t rom[8];
  ow.reset_search();
  while (ow.search(rom)) {
    s.onewire_total++;
    if (rom[0] == 0x28) s.ds18b20_count++;  // DS18B20 family code
  }
  ow.reset_search();

  s.last_scan_ms = millis();
  s.done = true;
  g_scan = s;
}

// Human-readable level for a digital input.
const char* lvl(int pin) { return digitalRead(pin) ? "HIGH" : "LOW"; }
}  // namespace

void initPins() {
  // Inputs we may safely drive. NOTE: GPIO34/35/36/39 are input-only and have
  // NO internal pull-ups — they need EXTERNAL pull-ups in the wiring.
  pinMode(PIN_DOOR, INPUT_PULLUP);   // 19 — has internal pull-up
  pinMode(PIN_HP_SWITCH, INPUT);     // 34 — needs external pull-up
  pinMode(PIN_LP_SWITCH, INPUT);     // 35 — needs external pull-up
  pinMode(PIN_FLOW, INPUT);          // 39
  pinMode(PIN_CURRENT_CLAMP, INPUT); // 36 (ADC1_CH0)
  // Relay/output pins (21,22,23,32,33) are intentionally left untouched.
}

void scan() { doScan(); }

void requestRescan() { g_rescanRequested = true; }

void serviceRescan() {
  if (g_rescanRequested) {
    g_rescanRequested = false;
    doScan();
  }
}

void toJson(JsonDocument& doc) {
  doc["scanned"] = g_scan.done;
  doc["scan_age_s"] = g_scan.done ? (millis() - g_scan.last_scan_ms) / 1000 : 0;

  // ── OneWire (auto-detected) ─────────────────────────────────
  JsonObject ow1 = doc["onewire"].to<JsonObject>();
  ow1["pin"] = PIN_ONEWIRE;
  ow1["ds18b20_found"] = g_scan.ds18b20_count;
  ow1["expected"] = 11;            // T1–T11 per the sensor plan
  ow1["devices_total"] = g_scan.onewire_total;

  // ── I2C (auto-detected) ─────────────────────────────────────
  JsonObject i2c = doc["i2c"].to<JsonObject>();
  i2c["sda"] = LCD_SDA_PIN;
  i2c["scl"] = LCD_SCL_PIN;
  JsonArray found = i2c["addresses"].to<JsonArray>();
  for (int i = 0; i < g_scan.i2c_count; i++) {
    char buf[6]; snprintf(buf, sizeof(buf), "0x%02X", g_scan.i2c_addr[i]);
    found.add(buf);
  }
  JsonArray idev = i2c["devices"].to<JsonArray>();
  { JsonObject d = idev.add<JsonObject>();
    d["label"] = "LCD 16x2"; d["addr"] = "0x27/0x3F"; d["present"] = g_scan.lcd_present; }
  { JsonObject d = idev.add<JsonObject>();
    d["label"] = "SHT31 Feuchte"; d["addr"] = "0x44"; d["present"] = g_scan.sht31_present; }

  // ── Digital inputs (live levels) ────────────────────────────
  JsonArray ins = doc["inputs"].to<JsonArray>();
  struct In { const char* label; int pin; const char* note; };
  static const In list[] = {
    {"Türkontakt D1",       PIN_DOOR,         "PullUp; offen=HIGH"},
    {"Hochdruckwächter P1", PIN_HP_SWITCH,    "ext. PullUp nötig!"},
    {"Niederdruckw. P2",    PIN_LP_SWITCH,    "ext. PullUp nötig!"},
    {"Durchfluss F1",       PIN_FLOW,         "Impuls"},
  };
  for (auto& in : list) {
    JsonObject o = ins.add<JsonObject>();
    o["label"] = in.label; o["pin"] = in.pin;
    o["level"] = lvl(in.pin); o["note"] = in.note;
  }

  // ── Analog ──────────────────────────────────────────────────
  JsonArray an = doc["analog"].to<JsonArray>();
  { JsonObject o = an.add<JsonObject>();
    o["label"] = "Stromzange SCT-013"; o["pin"] = PIN_CURRENT_CLAMP;
    o["raw"] = analogRead(PIN_CURRENT_CLAMP); o["max"] = 4095; }

  // ── Outputs (NOT driven in Phase 0 — no detection possible) ──
  JsonArray outs = doc["outputs"].to<JsonArray>();
  struct Out { const char* label; int pin; };
  static const Out olist[] = {
    {"K1 Kompressor", PIN_RELAY_K1}, {"K2 Blower", PIN_RELAY_K2},
    {"K3 Abtauheizung", PIN_RELAY_K3}, {"K4 Bypass-Ventil", PIN_RELAY_K4},
    {"K5 EVI-Ventil", PIN_RELAY_K5},
  };
  for (auto& o : olist) {
    JsonObject j = outs.add<JsonObject>();
    j["label"] = o.label; j["pin"] = o.pin; j["state"] = "Phase 1 (nicht angesteuert)";
  }

  // ── Modbus (Phase 1) ────────────────────────────────────────
  JsonArray mb = doc["modbus"].to<JsonArray>();
  { JsonObject o = mb.add<JsonObject>();
    o["label"] = "Softstarter ZSR4022"; o["bus"] = "RS485 #1 (UART2)"; o["status"] = "Phase 1"; }
  { JsonObject o = mb.add<JsonObject>();
    o["label"] = "Deye Wechselrichter"; o["bus"] = "RS485 #2 (UART1)"; o["status"] = "Phase 1"; }
}

}  // namespace hardware
