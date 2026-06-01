#pragma once
// web_server — AsyncWebServer + WebSocket. Phase 0 serves the dashboard from
// SPIFFS and pushes mock SystemState JSON every WS_PUSH_INTERVAL_MS.
// In later phases the mock generator is replaced by the real SystemState.
// See ARCHITECTURE.md §11.
#include <Arduino.h>

class AsyncWebServer;

namespace web_server {

// Registers all routes and the WebSocket endpoint, but does NOT start
// listening yet (so OTA can attach its /update route first). SPIFFS must
// already be mounted.
void begin();

// Begins listening. Call after any extra handlers (e.g. OTA) are attached.
void start();

// Returns the underlying AsyncWebServer (so OTA can attach to it).
AsyncWebServer& server();

// Broadcasts the current state JSON to all WebSocket clients.
// Phase 0: emits mock data. Call from the web task at WS_PUSH_INTERVAL_MS.
void broadcastState();

// Number of connected WebSocket clients.
uint32_t clientCount();

}  // namespace web_server
