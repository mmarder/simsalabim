#pragma once
// lcd_display — 16x2 I2C status display with phase-aware content.
// See ARCHITECTURE.md §6, spec §0.3.
#include <Arduino.h>

namespace lcd_display {

enum Screen : uint8_t {
  SCREEN_BOOT,       // "KK Samosir v0.1" / "Verbinde WiFi..."
  SCREEN_STATUS,     // IP + WiFi (Phase 0 idle)
  SCREEN_AP,         // captive-portal setup instructions
  SCREEN_UPDATE,     // OTA progress
  SCREEN_ALARM       // alarm banner (Phase 1+)
};

// Initialises the I2C LCD. Returns false if the display does not ACK on the
// configured address (firmware still runs headless — LCD is non-critical).
bool begin();

// Switches the active screen.
void setScreen(Screen s);

// Updates the two-line text for the STATUS screen.
void setStatus(const String& ip, int rssi);

// Updates the OTA progress percentage shown on the UPDATE screen (0..100).
void setUpdateProgress(int percent);

// Sets the two-line alarm banner.
void setAlarm(const String& line1, const String& line2);

// Call every LCD_UPDATE_MS from the lcd task. Redraws the active screen.
void tick();

}  // namespace lcd_display
