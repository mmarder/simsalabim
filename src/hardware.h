#pragma once
// hardware — peripheral detection & live pin status for the bring-up page.
// Auto-detects OneWire (DS18B20) and I2C devices; reads digital input levels
// and the analog clamp live. Does NOT touch the relay output pins (K1–K5):
// driving them in Phase 0 could energise the compressor/defrost relay, and the
// SSR on/off polarity is still unconfirmed. See COMMISSIONING.md / STATUS.md.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace hardware {

// Configure the input pins we are allowed to touch (door, HP, LP, flow, clamp).
// Output/relay pins are deliberately left undriven.
void initPins();

// Scan the OneWire and I2C buses and cache the result. Call once at boot
// (after Wire is initialised by the LCD) and from the I2C-owning task on
// rescan. Not safe to call concurrently with other I2C/OneWire access.
void scan();

// Ask for a rescan from another task (e.g. the web handler). The I2C-owning
// task performs it via serviceRescan().
void requestRescan();

// Called from the task that owns the I2C bus (lcd_task). Performs a scan if one
// was requested. Keeps all bus access on a single task to avoid races.
void serviceRescan();

// Fill `doc` with the full hardware status: cached bus scan + live input/analog
// reads. Safe to call from the web task (input reads are atomic).
void toJson(JsonDocument& doc);

}  // namespace hardware
