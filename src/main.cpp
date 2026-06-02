// =============================================================================
// main.cpp — Kältekammer ESP32, Phase 0 (WiFi + OTA + LCD + Web mockup)
//
// Phase 0 brings up the network stack, OTA paths, LCD, and the dashboard with
// mock data. The safety/control tasks (priority 4–5) are added in Phase 1.
// See DEVELOPMENT.md (process) and ARCHITECTURE.md §6.2 (task table).
// =============================================================================
#include <Arduino.h>
#include <SPIFFS.h>

#include "config.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "lcd_display.h"
#include "web_server.h"
#include "hardware.h"
#include "telemetry.h"

// LCD progress callback during the (blocking) WiFi connect attempt.
static void wifiProgress(const char* ssid, int elapsed_s, int max_s) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.10s %ds/%ds", ssid, elapsed_s, max_s);
  lcd_display::setBoot(buf);
}

// ── Task: web + WebSocket broadcast (priority 2) ─────────────────────────────
void webTask(void*) {
  web_server::begin();
  ota_manager::begin(web_server::server());  // attaches /update before listening
  web_server::start();
  for (;;) {
    web_server::broadcastState();
    ota_manager::loop();
    vTaskDelay(pdMS_TO_TICKS(WS_PUSH_INTERVAL_MS));
  }
}

// ── Task: LCD refresh (priority 2) ───────────────────────────────────────────
void lcdTask(void*) {
  for (;;) {
    // The LCD task owns the I2C bus, so it also services hardware rescans
    // (which probe I2C) to keep all bus access on one task.
    hardware::serviceRescan();
    if (wifi_manager::isConnected()) {
      lcd_display::setStatus(wifi_manager::ipAddress(), wifi_manager::rssi());
    }
    lcd_display::tick();
    vTaskDelay(pdMS_TO_TICKS(LCD_UPDATE_MS));
  }
}

// ── Task: WiFi housekeeping / captive portal (priority 1) ────────────────────
void netTask(void*) {
  for (;;) {
    wifi_manager::loop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ── Task: remote telemetry (priority 1, lowest) ──────────────────────────────
// Fire-and-forget POST of the status JSON. Lowest priority and fully isolated:
// it must never delay the control/safety loop. No-op unless configured.
void telemetryTask(void*) {
  for (;;) {
    if (telemetry::enabled() && wifi_manager::isConnected()) {
      telemetry::postOnce();   // blocks here only, in this low-prio task
    }
    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== " DEVICE_NAME " " FIRMWARE_VERSION " booting ===");

  lcd_display::begin();
  lcd_display::setBoot("Starte...");

  // SPIFFS holds /wifi.json, /config.json, and the web assets.
  if (!SPIFFS.begin(true)) {
    Serial.println("[FATAL] SPIFFS mount failed");
    lcd_display::setBoot("SPIFFS FEHLER!");
  } else {
    lcd_display::setBoot("SPIFFS OK");
  }

  // Configure safe-to-touch input pins, then scan the peripheral buses so the
  // hardware-status page has data immediately (Wire is up after lcd begin).
  hardware::initPins();
  lcd_display::setBoot("Scan Hardware..");
  hardware::scan();

  // Load telemetry config (SPIFFS override or compiled default).
  telemetry::begin();

  lcd_display::setBoot("WiFi suchen...");
  bool connected = wifi_manager::begin(wifiProgress);
  if (connected) {
    Serial.printf("[WiFi] connected: %s\n", wifi_manager::ipAddress().c_str());
    lcd_display::setBoot("OK " + wifi_manager::ipAddress());
    lcd_display::setStatus(wifi_manager::ipAddress(), wifi_manager::rssi());
  } else {
    Serial.printf("[WiFi] AP setup mode: http://%s\n",
                  wifi_manager::ipAddress().c_str());
    lcd_display::setScreen(lcd_display::SCREEN_AP);
  }

  // Pin web + LCD to core 1; networking housekeeping to core 0.
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(lcdTask, "lcd", 2048, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 4096, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(telemetryTask, "tele", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  // All work runs in FreeRTOS tasks. Nothing here.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
