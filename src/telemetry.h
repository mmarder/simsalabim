#pragma once
// telemetry — fire-and-forget HTTPS POST of the status JSON to a cloud endpoint
// (e.g. a Cloudflare Worker) for remote/online viewing. See cloud/ for the
// backend. STRICT rule: never block or interfere with the control/safety loop —
// runs in a low-priority task, failures are silent, fully disabled if not
// configured.
//
// Config precedence (like WiFi): a runtime config saved in SPIFFS
// (/telemetry.json, set via the web UI) OVERRIDES the compile-time defaults in
// config_secrets.h. This keeps the token out of the public firmware binary when
// set at runtime, while still allowing a baked default for OTA-only devices.
#include <Arduino.h>

namespace telemetry {

// Load the effective config (SPIFFS override, else compiled default). Call once
// at startup after SPIFFS is mounted.
void begin();

// True if a URL + token are configured (otherwise postOnce() is a no-op).
bool enabled();

// Posts the current status JSON once. Blocks (HTTPS); call only from the
// low-priority telemetry task. Returns true on HTTP 2xx. Silent on failure.
bool postOnce();

// ── Runtime configuration (web UI) ──────────────────────────────────────────
// Persist URL+token to SPIFFS and reload. Empty url clears the runtime config
// (reverts to the compiled default, if any).
void setConfig(const char* url, const char* token);

const char* url();          // effective URL (safe to display)
bool tokenSet();            // whether a token is set (never exposes the token)
const char* source();       // "runtime" | "compiled" | "none"

// ── Diagnostics (for the settings page) ─────────────────────────────────────
int lastStatus();                 // 0 = never, -1 = conn/TLS error, else HTTP code
uint32_t secondsSinceLastOk();    // 0 if never succeeded

}  // namespace telemetry
