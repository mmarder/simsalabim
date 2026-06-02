#pragma once
// wifi_manager — connects to stored WiFi, or opens a captive AP for setup.
// See ARCHITECTURE.md §6, spec §0.1.
#include <Arduino.h>

namespace wifi_manager {

// Progress callback invoked ~once per second during the connect attempt, so the
// caller can show live status (e.g. on the LCD). elapsed/max are in seconds.
typedef void (*ProgressFn)(const char* ssid, int elapsed_s, int max_s);

// Loads stored credentials and attempts connection.
// If no credentials exist or connection fails within WIFI_CONNECT_TIMEOUT_MS,
// opens the "KaeltekammerSetup" captive AP for configuration.
// `progress` (optional) is called each second during the connect wait.
// Returns true if connected as a station, false if running in AP setup mode.
bool begin(ProgressFn progress = nullptr);

// The SSID currently being used (from /wifi.json or the compiled default).
const char* currentSsid();

// True when connected to the configured WiFi network (station mode).
bool isConnected();

// Current IP address as a string ("192.168.4.1" in AP mode).
String ipAddress();

// WiFi RSSI in dBm (0 when in AP mode).
int rssi();

// Call periodically (from a low-priority task). Handles the captive portal
// DNS in AP mode and triggers a reconnect attempt if the link drops.
void loop();

}  // namespace wifi_manager
