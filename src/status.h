#pragma once
// status — builds the system-status JSON shared by the web API/WebSocket and
// the remote telemetry POST, so they never diverge. Phase 0 emits mock data
// (doc["mock"]=true); Phase 1 will populate it from the real SystemState.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace status {

// Fills `doc` with the current system snapshot (see ARCHITECTURE.md §11.2).
void buildJson(JsonDocument& doc);

}  // namespace status
