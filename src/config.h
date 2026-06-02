#pragma once
// =============================================================================
// config.h — Central configuration for Kältekammer ESP32
// Safe to commit. Secrets live in config_secrets.h (git-ignored).
// See ARCHITECTURE.md §3 (GPIO), §10 (parameters).
// =============================================================================

// ── Firmware identity ───────────────────────────────────────────────────────
#define FIRMWARE_VERSION   "v0.3"
#define DEVICE_NAME        "KK-Samosir"

// ── GitHub Releases OTA ─────────────────────────────────────────────────────
#define GITHUB_USER        "mmarder"
#define GITHUB_REPO        "simsalabim"
// GitHub OTA check interval (ms). 6 h.
#define GITHUB_CHECK_INTERVAL_MS  (6UL * 60UL * 60UL * 1000UL)

// ── OTA / Web auth ──────────────────────────────────────────────────────────
#define OTA_PASSWORD       "kk-samosir"
#define WEB_AUTH_USER      "admin"

// ── WiFi behaviour ──────────────────────────────────────────────────────────
#define WIFI_AP_SSID       "KaeltekammerSetup"     // captive AP on first boot
#define WIFI_CONNECT_TIMEOUT_MS   60000UL          // fall back to AP after 60 s
#define WIFI_CONFIG_FILE   "/wifi.json"

// ── LCD 16x2 I2C ────────────────────────────────────────────────────────────
#define LCD_I2C_ADDR       0x27       // 0x27 (PCF8574) or 0x3F (PCF8574A) — verify with scanner
#define LCD_COLS           16
#define LCD_ROWS           2
#define LCD_SDA_PIN        13         // alt I2C (21/22 reserved for relays K1/K2)
#define LCD_SCL_PIN        14
#define LCD_UPDATE_MS      2000       // refresh interval (LCD flickers if faster)

// ── GPIO pin map (full system — Phase 1+ pins declared now for reference) ────
#define PIN_ONEWIRE        4
#define PIN_RS485_1_RX     16    // soft starter UART2
#define PIN_RS485_1_TX     17
#define PIN_RS485_1_DIR    18
#define PIN_DOOR           19
#define PIN_RELAY_K1       21    // compressor start (fallback)
#define PIN_RELAY_K2       22    // blower fans
#define PIN_RELAY_K3       23    // defrost heater
#define PIN_RS485_2_RX     25    // Deye UART1
#define PIN_RS485_2_TX     26
#define PIN_RS485_2_DIR    27
#define PIN_RELAY_K4       32    // bypass valve
#define PIN_RELAY_K5       33    // EVI solenoid
#define PIN_HP_SWITCH      34    // high-pressure switch P1
#define PIN_LP_SWITCH      35    // low-pressure switch P2
#define PIN_CURRENT_CLAMP  36    // SCT-013 (ADC)
#define PIN_FLOW           39    // YF-S201

// ── Web server ──────────────────────────────────────────────────────────────
#define WEB_PORT           80
#define WS_PUSH_INTERVAL_MS  1000     // WebSocket broadcast cadence

// ── Sensor sentinel ─────────────────────────────────────────────────────────
#define SENSOR_ERROR       (-127.0f)  // DS18B20 invalid-reading marker
#define SENSOR_MIN_VALID   (-60.0f)
#define SENSOR_MAX_VALID   (150.0f)

// =============================================================================
// Secrets (WiFi credentials, GitHub token). Local file, never committed.
// Create from config_secrets.h.example on a fresh checkout.
// =============================================================================
#include "config_secrets.h"
