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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== " DEVICE_NAME " " FIRMWARE_VERSION " booting ===");

  // SPIFFS holds /wifi.json, /config.json, and the web assets.
  if (!SPIFFS.begin(true)) {
    Serial.println("[FATAL] SPIFFS mount failed");
  }

  lcd_display::begin();
  lcd_display::setScreen(lcd_display::SCREEN_BOOT);

  bool connected = wifi_manager::begin();
  if (connected) {
    Serial.printf("[WiFi] connected: %s\n", wifi_manager::ipAddress().c_str());
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
}

void loop() {
  // All work runs in FreeRTOS tasks. Nothing here.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
