# Kältekammer ESP32 — Architecture Reference

**Standort:** Samosir Island, Lake Toba, Sumatra, Indonesia (~900 m NN)  
**Betrieb:** Speiseeis-Lagerung 10.000 kg | Ziel -18 bis -22°C | Tiefkühl bis -35°C  
**Firmware:** Arduino Framework / FreeRTOS on ESP32 DevKit V1  
**Spec:** `../Docs/kaeltekammer_steuerung_spec.md` v1.6

Any developer or AI agent can read this file and have a full mental model of the firmware without exploring the codebase. Keep it up to date — an outdated architecture doc is worse than none.

---

## 1. Project Overview

The system maximises solar PV self-consumption by running the compressor primarily during daylight hours, deep-cooling the 10,000 kg ice product to −35°C, then coasting on the thermal mass of the product through the night. Heat is recovered into a 1,000 L hot-water buffer (target 55–65°C) and a 10,000 L whirlpool (target 38°C).

**Control hardware:** ESP32 DevKit V1  
**Compressor:** Copeland ZF 18 KVE hermetic scroll, R404A, with EVI (Enhanced Vapor Injection)  
**Soft starter:** Zenith ZSR4022-A-3P3, Modbus RTU  
**Inverter:** Deye SUN-15K-G06-P3-EU BM2 P1, 12 kWp PV, Modbus RTU  

---

## 2. Hardware Inventory

| Component | Model | Interface | Notes |
|-----------|-------|-----------|-------|
| Microcontroller | ESP32 DevKit V1 | — | 5V USB / 3.3V I/O |
| Compressor | Copeland ZF 18 KVE | — | R404A, EVI, ~2.0–2.5 kW el |
| Soft starter | Zenith ZSR4022-A-3P3 | RS485 Modbus RTU | 9600/8E1/addr 1; UART2 |
| Inverter | Deye SUN-15K-G06-P3-EU BM2 P1 | RS485 Modbus RTU | 9600/8N1/addr 1; UART1 |
| Temp sensors | DS18B20 (×11) | OneWire | GPIO 4; 4.7 kΩ pullup to 3.3V |
| Humidity | SHT31 module | I²C | GPIO 13 (SDA) / 14 (SCL); addr 0x44 |
| LCD display | 16×2 + PCF8574 | I²C | GPIO 13/14; addr 0x27 or 0x3F |
| RS485 #1 | MAX485 module | UART2 | Soft starter, GPIO 16/17/18 |
| RS485 #2 | MAX485 module | UART1 | Deye inverter, GPIO 25/26/27 |
| Door contact | Reed switch | Digital IN | GPIO 19, pullup |
| HP pressure switch | NC contact P1 | Digital IN | GPIO 34, pullup |
| LP pressure switch | NC contact P2 | Digital IN | GPIO 35, pullup |
| Relay K1 | SSR 10A | Digital OUT | GPIO 21 — Compressor start (backup/fallback) |
| Relay K2 | SSR 10A | Digital OUT | GPIO 22 — Blower fans |
| Relay K3 | SSR 10A | Digital OUT | GPIO 23 — Defrost heater |
| Relay K4 | SSR 10A | Digital OUT | GPIO 32 — Bypass valve WW+Pool ⚠️ voltage TBD |
| Relay K5 | SSR 10A | Digital OUT | GPIO 33 — EVI solenoid valve ⚠️ voltage TBD |
| Current clamp | SCT-013-030 (30A) | Analog IN | GPIO 36 — Compressor L1 |
| Flow sensor | YF-S201 | Interrupt IN | GPIO 39 — WW circuit flow |
| I²C bus | SDA=GPIO13, SCL=GPIO14 | — | SHT31 + LCD share bus |

---

## 3. GPIO Allocation

| GPIO | Direction | Function | Notes |
|------|-----------|----------|-------|
| 4 | I/O | OneWire bus (T1–T11) | 4.7 kΩ pullup |
| 13 | I/O | I²C SDA (SHT31 + LCD) | Shared bus |
| 14 | I/O | I²C SCL (SHT31 + LCD) | Shared bus |
| 16 | IN | RS485 #1 RX — Soft starter (UART2) | |
| 17 | OUT | RS485 #1 TX — Soft starter (UART2) | |
| 18 | OUT | RS485 #1 DIR — Soft starter DE/RE | |
| 19 | IN | Door contact D1 | INPUT_PULLUP |
| 21 | OUT | Relay K1 — Compressor start fallback | HIGH = on |
| 22 | OUT | Relay K2 — Blower fans | HIGH = on |
| 23 | OUT | Relay K3 — Defrost heater | HIGH = on |
| 25 | IN | RS485 #2 RX — Deye (UART1) | |
| 26 | OUT | RS485 #2 TX — Deye (UART1) | |
| 27 | OUT | RS485 #2 DIR — Deye DE/RE | |
| 32 | OUT | Relay K4 — Bypass valve | HIGH = open |
| 33 | OUT | Relay K5 — EVI solenoid | HIGH = open |
| 34 | IN | HP pressure switch P1 | INPUT_PULLUP; open = alarm |
| 35 | IN | LP pressure switch P2 | INPUT_PULLUP; open = alarm |
| 36 | IN | SCT-013 current clamp (ADC) | Analog; 10 kΩ burden resistor |
| 39 | IN | YF-S201 flow meter (interrupt) | INPUT_PULLUP |

---

## 4. OneWire Sensor Map

All DS18B20 sensors share GPIO 4. Sensors are identified by their unique 64-bit ROM address, not by bus position. Addresses must be discovered with a scanner sketch and stored in `config.h` during commissioning.

| ID | Sensor | Position |
|----|--------|----------|
| T1 | Kammer oben | Decke Mitte |
| T2 | Kammer Mitte | 1,5 m, Türgegenüber |
| T3 | Kammer unten | 0,3 m über Boden |
| T4 | Kompressorkopf / Druckgas | Druckgasleitung — Wärmeleitpaste + Armaflex |
| T5 | Sauggas | Vor Kompressor — Wärmeleitpaste + Armaflex |
| T6 | WW-Puffer oben | Obere Zone |
| T7 | WW-Puffer unten | Untere Zone |
| T8 | Außentemperatur | Schattiger Außenplatz, min. 1 m von Wand |
| T9 | Verdampfer / Blower | Lamellen Ein/Ausgang |
| T10 | BPHE WW-Austritt | WW-Ausgang BPHE |
| T11 | Whirlpool | Wassertemperatur |

**Validation rule:** Any DS18B20 reading outside −60.0°C to +150.0°C is treated as a sensor error (`SENSOR_ERROR = -127.0f`). Error values must never be used in control decisions — they trigger the appropriate alarm flag.

---

## 5. Modbus Communication

### 5.1 RS485 Channel 1 — Soft Starter Zenith ZSR4022

| Parameter | Value |
|-----------|-------|
| UART | UART2 (GPIO 16 RX / 17 TX / 18 DIR) |
| Baud | 9600 |
| Frame | 8E1 (8 data, Even parity, 1 stop) |
| Slave address | 1 |
| Library | ModbusMaster |

**Register map:** ⚠️ NOT YET VERIFIED — see Open Items §12 item SS6. The following are placeholders based on generic soft starter conventions. Do not use in production code until confirmed against the ZSR4022 Modbus chapter.

| Function | Register (tentative) | Type | Notes |
|----------|---------------------|------|-------|
| Start command | TBD | W | |
| Stop command | TBD | W | |
| Motor current (A) | TBD | R | |
| Status word | TBD | R | Running / fault flags |
| Fault code | TBD | R | Maps to A04–A09 |

**Fault LED decode (from manual):**

| Fault LED 1 | Fault LED 2 | Alarm | Code |
|-------------|-------------|-------|------|
| ○ | ○ | Phasenfolge falsch | A04 |
| ○ | ● | Phasenausfall / Keine Spannung | A05 |
| ○ | ● blink | Überstrom | A06 |
| ● | ○ | Überlast | A07 |
| ● | ● | Stromunsymmetrie | A08 |
| ○ blink | ● | Übertemperatur Kühlkörper | A09 |

**Fallback (A15 — RS485 failure):** If 3 consecutive Modbus transactions fail, the firmware switches to relay-only mode (K1) and raises alarm A15. Control precision is reduced but compressor can still run.

---

### 5.2 RS485 Channel 2 — Deye Inverter SUN-15K-G06-P3-EU

| Parameter | Value |
|-----------|-------|
| UART | UART1 (GPIO 25 RX / 26 TX / 27 DIR) |
| Baud | 9600 |
| Frame | 8N1 (8 data, No parity, 1 stop) |
| Slave address | 1 |
| Library | ModbusMaster |

**Verified register map (community-verified, G06/SG04LP3 family):**

| Metric | Register (dec) | Register (hex) | Type | Scale | Unit |
|--------|---------------|---------------|------|-------|------|
| PV1 power | 672 | 0x2A0 | U_WORD | ×1 | W |
| PV2 power | 673 | 0x2A1 | U_WORD | ×1 | W |
| PV1 voltage | 676 | 0x2A4 | U_WORD | ×0.1 | V |
| PV2 voltage | 678 | 0x2A6 | U_WORD | ×0.1 | V |
| Grid total power | 625 | 0x271 | **S_WORD** | ×1 | W (+ = export, − = import) |
| Grid L1 voltage | 598 | 0x256 | U_WORD | ×0.1 | V |
| Grid L2 voltage | 599 | 0x257 | U_WORD | ×0.1 | V |
| Grid L3 voltage | 600 | 0x258 | U_WORD | ×0.1 | V |
| CT intern L1 | 604 | 0x25C | S_WORD | ×1 | W |
| CT intern L2 | 605 | 0x25D | S_WORD | ×1 | W |
| CT intern L3 | 606 | 0x25E | S_WORD | ×1 | W |
| Day energy | 529 | 0x211 | U_WORD | ×0.1 | kWh |
| Total energy | 534/535 | 0x216/0x217 | U_DWORD | ×0.1 | kWh |
| Day grid import | 520 | 0x208 | U_WORD | ×0.1 | kWh |
| Day grid export | 521 | 0x209 | U_WORD | ×0.1 | kWh |
| Inverter status | 500 | 0x1F4 | U_WORD | — | — |

> ⚠️ Register 625 is a **signed** 16-bit word. Must be cast to `int16_t` before use.  
> Negative = grid import (buying), positive = export (selling).  
> PV total = reg 672 + reg 673.

**Fallback (A14 — RS485 failure):** If 3 consecutive reads fail, PV data switches to the local solar forecast model (`calc_solar_watt()`). The alarm is set and shown in the dashboard.

---

## 6. Software Architecture

### 6.1 Repository Layout

```
kaeltekammer-esp32/
├── platformio.ini
├── partitions_ota.csv
├── .gitignore                        ← config_secrets.h excluded
├── src/
│   ├── main.cpp                      Setup, task creation, SPIFFS mount
│   ├── config.h                      All constants, pin defs, FIRMWARE_VERSION
│   │                                 #include "config_secrets.h" (not in repo)
│   ├── config_secrets.h              WiFi credentials, GitHub token (git-ignored; .example committed)
│   ├── SystemState.h                 Central state struct + mutex            [Phase 1 — planned]
│   ├── wifi_manager.h / .cpp         AP setup, SPIFFS persistence, connect progress cb   [Phase 0 ✓]
│   ├── ota_manager.h / .cpp          ElegantOTA + GitHub fw/fs OTA + auto-deploy         [Phase 0 ✓]
│   ├── lcd_display.h / .cpp          16×2 I2C, staged boot messages, screen rotation     [Phase 0 ✓]
│   ├── web_server.h / .cpp           AsyncWebServer, REST API, WebSocket                 [Phase 0 ✓]
│   ├── hardware.h / .cpp             OneWire/I²C detection + live pin status (/api/hardware) [Phase 0 ✓]
│   ├── status.h / .cpp               Shared status-JSON builder (web + telemetry)        [Phase 0 ✓]
│   ├── telemetry.h / .cpp            Fire-and-forget status POST to cloud (SPIFFS/compiled cfg) [Phase 0 ✓]
│   ├── ota_version.h                 Host-tested version logic (header-only, no Arduino)  [Phase 0 ✓]
│   ├── sensors/                      [Phase 1 — planned]
│   │   ├── temperature.h / .cpp      DS18B20 OneWire, 11 sensors, address map
│   │   ├── humidity.h / .cpp         SHT31 I²C
│   │   └── power.h / .cpp            SCT-013 RMS calculation, YF-S201 pulse count
│   ├── control/
│   │   ├── controller.h / .cpp       Main control task (10 s), mode dispatch
│   │   ├── modes.h / .cpp            Operating mode state machine
│   │   ├── safety.h / .cpp           Safety task (1 s), alarm flags, emergency stop
│   │   ├── defrost.h / .cpp          Defrost state machine
│   │   ├── evi.h / .cpp              EVI solenoid logic
│   │   └── hotwater.h / .cpp         WW-Puffer / Whirlpool / bypass logic
│   ├── communication/
│   │   ├── modbus_softstarter.h/.cpp Zenith RS485 UART2
│   │   └── modbus_deye.h / .cpp      Deye RS485 UART1
│   ├── storage/
│   │   ├── config_store.h / .cpp     Read/write config JSON to SPIFFS
│   │   └── datalog.h / .cpp          Ring buffer: 7 days × 1 min, SPIFFS
│   └── energy/
│       ├── pv_forecast.h / .cpp      Solar position model, Samosir coords
│       └── scheduler.h / .cpp        Load planning, night strategy
├── data/                             SPIFFS web files (uploaded separately)
│   ├── index.html                    Live dashboard + OTA card             [Phase 0 ✓]
│   ├── settings.html                 Live hardware-status page             [Phase 0 ✓]
│   ├── app.js / style.css            Dashboard logic + styles              [Phase 0 ✓]
│   ├── history.html                  24h / 7d charts                       [Phase 2 — planned]
│   └── alarms.html                   Alarm log                             [Phase 2 — planned]
├── cloud/                            Telemetry backend (Cloudflare Worker + KV)  [Phase 0 ✓]
│   ├── worker.js                     Ingest (POST) + public viewer (GET) + /data
│   ├── wrangler.toml                 Worker config (KV binding; secret set separately)
│   └── README.md                     Deploy steps
├── .github/workflows/ci.yml          CI: native tests + esp32 build on push/PR   [Phase 0 ✓]
└── test/
    ├── test_version/                 ✅ OTA version logic (implemented)
    ├── test_modes/                   Mode determination tests
    ├── test_safety/                  Safety threshold and latch tests
    ├── test_defrost/                 Defrost state machine tests
    ├── test_evi/                     EVI open/close condition tests
    ├── test_hotwater/                WW/pool switching logic tests
    ├── test_pv_forecast/             Solar model output tests
    └── test_sensors/                 DS18B20 validation and error handling
```

### 6.2 FreeRTOS Task Structure

| Task | Priority | Stack | Interval | Function |
|------|----------|-------|----------|----------|
| `safety_task` | 5 (highest) | 4 KB | 1 s | HP/LP/temp alarms, emergency stop |
| `control_task` | 4 | 8 KB | 10 s | Mode determination, compressor control |
| `modbus_ss_task` | 3 | 4 KB | 5 s | Soft starter read/write (UART2) |
| `modbus_deye_task` | 3 | 4 KB | 10 s | Deye PV+grid data (UART1) |
| `sensor_task` | 3 | 6 KB | 10 s | DS18B20, SHT31, SCT-013, flow |
| `web_task` | 2 | 8 KB | continuous | AsyncWebServer + WebSocket push |
| `lcd_task` | 2 | 2 KB | 2 s | LCD screen update |
| `log_task` | 1 (lowest) | 4 KB | 60 s | Write state to SPIFFS ring buffer |
| `forecast_task` | 1 | 4 KB | 15 min | Recalculate PV forecast |

**Concurrency:** `SystemState` is the single shared data structure. All tasks read and write through a single `SemaphoreHandle_t state_mutex`. Lock time must be minimal — copy what you need, release the mutex, then do computation.

---

## 7. Central Data Model

### 7.1 SystemState

```cpp
// SystemState.h — the single source of truth shared across all tasks.
// All fields are plain POD types for safe mutex-protected copy.

enum OperatingMode : uint8_t {
  MODE_NORMAL    = 0,
  MODE_DEEP_COOL = 1,
  MODE_ECONOMY   = 2,
  MODE_BUFFER    = 3,
  MODE_HOTWATER  = 4,
  MODE_DEFROST   = 5,
  MODE_ALARM     = 6,
  MODE_STANDBY   = 7
};

enum DefrostPhase : uint8_t {
  DEFROST_IDLE       = 0,
  DEFROST_COMP_STOP  = 1,   // t=0:    compressor stopped
  DEFROST_BLOWER_OFF = 2,   // t=30s:  blower stopped
  DEFROST_HEAT_ON    = 3,   // t=60s:  heater on
  DEFROST_WAIT       = 4,   // waiting for T_verd > +5°C (or timeout 35 min)
  DEFROST_DRIP       = 5,   // t+2min: drip time
  DEFROST_BLOWER_ON  = 6,   // t+5min: blower restart
  DEFROST_DONE       = 7    // t+10min: compressor may restart
};

// Alarm bitmask — one bit per alarm code
#define ALARM_A01  (1UL << 0)   // Druckgas > 115°C — KRITISCH
#define ALARM_A02  (1UL << 1)   // Hochdruckwächter — KRITISCH
#define ALARM_A03  (1UL << 2)   // Niederdruckwächter — KRITISCH
#define ALARM_A04  (1UL << 3)   // SS: Phasenfolge falsch
#define ALARM_A05  (1UL << 4)   // SS: Phasenausfall
#define ALARM_A06  (1UL << 5)   // SS: Überstrom
#define ALARM_A07  (1UL << 6)   // SS: Überlast
#define ALARM_A08  (1UL << 7)   // SS: Stromunsymmetrie
#define ALARM_A09  (1UL << 8)   // SS: Übertemperatur Kühlkörper
#define ALARM_A10  (1UL << 9)   // Kammer > -15°C
#define ALARM_A11  (1UL << 10)  // Sensor Ausfall
#define ALARM_A12  (1UL << 11)  // Tür offen > 2 min
#define ALARM_A13  (1UL << 12)  // WW > 70°C
#define ALARM_A14  (1UL << 13)  // Deye RS485 Fehler
#define ALARM_A15  (1UL << 14)  // SS RS485 Fehler
#define ALARM_A16  (1UL << 15)  // Druckgas > 100°C — Warnung

struct SystemState {
  // ── Temperatures (°C; SENSOR_ERROR = -127.0f means invalid)
  float T_kammer[3];      // T1, T2, T3
  float T_kammer_mean;    // mean of valid T1/T2/T3 readings
  float T_kopf;           // T4 — compressor head / discharge line
  float T_saug;           // T5 — suction gas
  float T_ww_oben;        // T6 — hot water buffer top
  float T_ww_unten;       // T7 — hot water buffer bottom
  float T_aussen;         // T8 — outdoor ambient
  float T_verd;           // T9 — evaporator / blower
  float T_bphe;           // T10 — BPHE hot water outlet
  float T_whirlpool;      // T11 — whirlpool water
  float RH_kammer;        // % relative humidity inside chamber

  // ── PV & Grid (from Deye Modbus or forecast fallback)
  float pv_power_w;        // PV1 + PV2 total (W)
  float pv1_power_w;       // PV string 1 (W)
  float pv2_power_w;       // PV string 2 (W)
  float grid_power_w;      // + = import (buying), − = export (selling)
  float pv_day_kwh;        // daily PV yield (kWh)
  float pv_forecast_w[8];  // forecast for next 8 hours (W)
  bool  pv_data_valid;     // false = using forecast fallback (A14 active)

  // ── Soft Starter (from Modbus or estimated from SCT-013)
  float    ss_current_a;   // motor current (A)
  uint16_t ss_status;      // status register raw
  uint16_t ss_fault;       // fault code register raw
  uint32_t ss_runtime_h;   // total runtime hours (if available)
  bool     ss_data_valid;  // false = RS485 failed (A15 active)

  // ── Measurements
  float comp_power_w;      // compressor power from SCT-013 (W)
  float ww_flow_lmin;      // hot water flow (L/min) from YF-S201

  // ── Actuator states (source of truth for all outputs)
  bool comp_running;       // true = compressor on (Modbus or relay K1)
  bool blower_on;          // true = K2 energised
  bool defrost_on;         // true = K3 energised
  bool bypass_open;        // true = K4 open (bypass active)
  bool evi_open;           // true = K5 open (EVI active)
  bool door_open;          // true = door contact triggered

  // ── Pressure switches
  bool hp_ok;              // true = P1 closed (normal)
  bool lp_ok;              // true = P2 closed (normal)

  // ── Control state
  OperatingMode mode;
  DefrostPhase  defrost_phase;
  float         T_setpoint;    // current target temperature (°C)

  // ── Timing (seconds since boot, from millis()/1000)
  uint32_t comp_on_s;          // seconds since compressor started
  uint32_t comp_off_s;         // seconds since compressor stopped
  uint32_t door_open_s;        // seconds door has been open continuously
  uint32_t last_defrost_ts;    // timestamp of last completed defrost
  uint32_t defrost_phase_ts;   // timestamp of last defrost phase transition

  // ── Alarms
  uint32_t alarm_flags;        // bitmask, see ALARM_A01..A16

  // ── Misc
  uint32_t timestamp;          // current Unix time (from ESP32Time / NTP)
  uint32_t uptime_s;           // seconds since boot
  char     firmware_version[16];
};
```

### 7.2 SPIFFS File Layout

| Path | Format | Content |
|------|--------|---------|
| `/wifi.json` | JSON | `{"ssid":"...","password":"..."}` |
| `/config.json` | JSON | All runtime-adjustable parameters (see §10) |
| `/log/YYYYMMDD.bin` | Binary ring buffer | 1440 entries/day × `LogEntry` struct |

---

## 8. Control Logic

### 8.1 Operating Mode Determination

Called every 10 s by `control_task`. Returns the mode with the highest priority whose conditions are met.

```
Priority order (highest first):
  1. ALARM     — any CRITICAL alarm flag set
  2. STANDBY   — door open > 120 s
  3. DEFROST   — defrost sequence in progress
  4. DEEP_COOL — PV > PV_THRESHOLD_DEEP (default 3000 W)
                 AND grid_import < 200 W
                 AND T_kammer_mean > DEEP_COOL_SETPOINT + 2.0 K (= −33°C)
  5. HOTWATER  — T_ww_oben < WW_SOLL_EIN (default 45°C)
                 AND pv_power_w > PV_THRESHOLD_WW (default 2000 W)
  6. ECONOMY   — hour in [0, 6)  AND T_kammer_mean < −18.0°C
  7. BUFFER    — hour in [0, 6)  AND T_kammer_mean ≥ −18.0°C
  8. NORMAL    — default
```

### 8.2 Compressor Start / Stop Conditions

**START** (in `control_task`, never in `safety_task`):
1. `T_kammer_mean > T_setpoint + hyst_on` (mode-dependent hysteresis)
2. `comp_off_s >= MIN_STANDZEIT` (default 180 s)
3. No CRITICAL alarm active
4. Not in DEFROST or STANDBY mode
5. Send Modbus start command (or activate K1 relay in fallback mode)

**STOP** (normal, in `control_task`):
1. `T_kammer_mean < T_setpoint − hyst_off`
2. `comp_on_s >= MIN_LAUFZEIT` (default 300 s)
3. Send Modbus stop command + deactivate K1

**STOP** (safety, in `safety_task`, immediate, unconditional):
- Any CRITICAL alarm (A01, A02, A03)
- `T_kopf > 115°C` → also sets ALARM_A01
- `!hp_ok` → sets ALARM_A02
- `!lp_ok` → sets ALARM_A03

### 8.3 EVI Solenoid Control

Evaluated every 10 s in `control_task`.

```
OPEN conditions (ALL must be true):
  comp_on_s > 30 s
  T_verd < EVI_OPEN_BELOW (default −15.0°C)
  (T_saug − T_verd) > EVI_SUPERHEAT_MIN_OPEN (default 12.0 K)

CLOSE conditions (ANY sufficient):
  !comp_running
  T_verd > EVI_CLOSE_ABOVE (default −12.0°C)
  (T_saug − T_verd) < EVI_SUPERHEAT_MIN_CLOSE (default 8.0 K)

NOTE: EVI valve closes unconditionally when compressor stops.
This is enforced in the compressor stop routine, not only in the EVI loop.
```

### 8.4 Defrost State Machine

State transitions in `DefrostPhase` enum. All phase durations are tracked via `defrost_phase_ts`.

```
IDLE → COMP_STOP   : trigger condition met (see §8.5)
COMP_STOP → BLOWER_OFF   : 30 s elapsed
BLOWER_OFF → HEAT_ON     : 30 s elapsed (= 60 s after compressor stop)
HEAT_ON → WAIT           : immediately (heater relay K3 on)
WAIT → DRIP              : T_verd > +5.0°C  OR  35 min timeout
DRIP → BLOWER_ON         : 2 min elapsed (drip time)
BLOWER_ON → DONE         : 5 min elapsed (air circulation)
DONE → IDLE              : 10 min elapsed (compressor restart allowed)
```

**Sperrbedingungen (defrost cannot start):**
- `T_kammer_mean > −16.0°C` — chamber too warm, defrost would hurt product
- Hour in [22, 24) or [0, 6) — night block (except EMERGENCY trigger)
- Defrost completed within `DEFROST_MIN_INTERVAL`

### 8.5 Defrost Trigger Conditions

| Trigger | Condition | Night Block | Notes |
|---------|-----------|-------------|-------|
| Timer (cold) | `now − last_defrost_ts > 4 h` AND `T_kammer < −25°C` | yes | Standard |
| Timer (normal) | `now − last_defrost_ts > 6 h` | yes | Standard |
| Emergency | `T_verd < −32°C` AND `comp_on_s > 3600 s` | **no** | Heavy icing |
| Manual | Web dashboard button | yes | Operator |

### 8.6 Hot Water / Bypass Management

Evaluated every 10 s. Controls relay K4 (bypass valve).

```
comp_running AND T_ww_oben < WW_SOLL_OBEN (65°C):
  → K4 closed — heat goes to WW buffer (Prio 1)

T_ww_oben >= WW_SOLL_OBEN AND T_whirlpool < POOL_SOLL (38°C):
  → switch to pool circuit (Prio 2)

T_ww_oben >= WW_SOLL_OBEN AND T_whirlpool >= POOL_SOLL:
  → K4 open — bypass (air/ambient condenser)

T_ww_oben > WW_ALARM_MAX (70°C):
  → K4 open + ALARM_A13 (regardless of pool temp)
```

---

## 9. Testing — Mandatory Test Inventory

Every function in this list must have unit tests in `test/`. Tests run on the native host (no hardware) via `pio test -e native`. Add new functions to this list when implemented.

| Module | Function | Key test cases |
|--------|----------|---------------|
| `modes.cpp` | `determine_mode()` | ALARM beats everything; STANDBY beats non-alarm; DEEP_COOL requires all 3 conditions; ECONOMY only at night with cold chamber; BUFFER at night with warm chamber; NORMAL as default |
| `modes.cpp` | `determine_mode()` | T_kammer_mean = SENSOR_ERROR input |
| `safety.cpp` | `check_safety()` | T_kopf > 115 → A01 + comp stop; T_kopf = 115.0 → no alarm; T_kopf = SENSOR_ERROR → comp stop (fail-safe) |
| `safety.cpp` | `check_safety()` | HP open → A02; LP open → A03; both open → both alarms simultaneously |
| `safety.cpp` | `check_safety()` | Alarm latches: condition clears but flag remains until explicit reset |
| `controller.cpp` | `may_start_compressor()` | min-off-time < 180 s → false; ≥ 180 s → true; any CRITICAL alarm → false; DEFROST mode → false |
| `controller.cpp` | `may_stop_compressor()` | min-on-time < 300 s, no safety → false; safety flag set → always true; min-on-time ≥ 300 s, no alarm → true |
| `evi.cpp` | `evaluate_evi()` | All 3 open conditions met → opens; any one close condition → closes; comp off → always closes |
| `evi.cpp` | `evaluate_evi()` | comp_on_s = 29 → does not open; comp_on_s = 31 → may open |
| `evi.cpp` | `evaluate_evi()` | T_saug or T_verd = SENSOR_ERROR → does not open |
| `defrost.cpp` | `should_start_defrost()` | Timer trigger respects interval; night block prevents start; emergency trigger ignores night block; T_kammer > −16 → no start |
| `defrost.cpp` | `advance_defrost_phase()` | Each phase transitions on correct elapsed time; WAIT exits on T_verd > +5°C; WAIT times out at 35 min; heater K3 only on in HEAT_ON + WAIT |
| `hotwater.cpp` | `evaluate_bypass()` | WW < 65 → closed; WW ≥ 65 and pool < 38 → pool; both satisfied → bypass; WW > 70 → bypass + A13 |
| `pv_forecast.cpp` | `calc_solar_watt()` | Before sunrise → 0 W; after sunset → 0 W; solar noon → near PV_PEAK × ETA; midnight → 0 W |
| `sensors/temperature.cpp` | `validate_temperature()` | −127.0 (DS18B20 CRC error) → SENSOR_ERROR; −60.0 → valid; +150.0 → valid; +150.1 → SENSOR_ERROR; +85.0 (DS18B20 power-on default) → SENSOR_ERROR |
| `modbus_deye.cpp` | `parse_grid_power()` | 0x0064 (+100 W export) → +100.0f; 0xFFFF (−1 in S_WORD) → −1.0f; 0x8000 → −32768.0f (max import) |
| `ota_version.h` ✅ | `is_newer()` / `is_auto_tag()` | **Implemented + tested** (`test/test_version`, 9 cases): minor/major ordering, multi-digit, 'a' suffix ignored in compare, equal→not-newer (loop guard), bad/empty/null input fail-safe, auto-tag detection |

**CI:** `.github/workflows/ci.yml` runs `pio test -e native` + `pio run -e esp32dev`
on every push/PR (toolchain cached). The native test env (`test_build_src = no`)
compiles only the tests + header-only pure modules — no Arduino sources — so
business logic must stay free of Arduino deps to be testable on the host.

---

## 10. Configuration Parameters

All parameters live in `config.h` with compile-time defaults. Runtime-adjustable parameters are also persisted in `/config.json` on SPIFFS.

| Parameter | Default | Min | Max | Runtime |
|-----------|---------|-----|-----|---------|
| `T_SOLL_NORMAL` | −20.0°C | −25 | −15 | yes |
| `T_SOLL_DEEP_COOL` | −35.0°C | −38 | −28 | yes |
| `T_SOLL_ECONOMY` | −18.0°C | −22 | −15 | yes |
| `HYST_ON_NORMAL` | 1.0 K | 0.5 | 3.0 | yes |
| `HYST_OFF_NORMAL` | 1.0 K | 0.5 | 3.0 | yes |
| `MIN_STANDZEIT` | 180 s | 60 | 600 | yes |
| `MIN_LAUFZEIT` | 300 s | 120 | 900 | yes |
| `PV_THRESHOLD_DEEP` | 3000 W | 1000 | 8000 | yes |
| `PV_THRESHOLD_WW` | 2000 W | 500 | 6000 | yes |
| `WW_SOLL_EIN` | 45.0°C | 40 | 60 | yes |
| `WW_SOLL_OBEN` | 65.0°C | 50 | 70 | yes |
| `WW_ALARM_MAX` | 70.0°C | — | — | no |
| `POOL_SOLL` | 38.0°C | 30 | 42 | yes |
| `DEFROST_INTERVAL_COLD` | 14400 s (4 h) | 7200 | 28800 | yes |
| `DEFROST_INTERVAL_NORMAL` | 21600 s (6 h) | 14400 | 43200 | yes |
| `DEFROST_TIMEOUT` | 2100 s (35 min) | — | — | no |
| `EVI_OPEN_BELOW` | −15.0°C | −20 | −10 | yes |
| `EVI_CLOSE_ABOVE` | −12.0°C | −18 | −8 | yes |
| `EVI_SUPERHEAT_MIN_OPEN` | 12.0 K | 8 | 20 | yes |
| `EVI_SUPERHEAT_MIN_CLOSE` | 8.0 K | 5 | 15 | yes |
| `T_KOPF_ALARM` | 115.0°C | — | — | no |
| `T_KOPF_WARNING` | 100.0°C | — | — | no |
| `SS_MODBUS_RETRY` | 3 | — | — | no |
| `DEYE_MODBUS_RETRY` | 3 | — | — | no |
| `OTA_PASSWORD` | "kk-samosir" | — | — | no |
| `LCD_I2C_ADDR` | 0x27 | — | — | no (set at commissioning) |
| `FIRMWARE_VERSION` | "v0.1" | — | — | no (set at build) |

---

## 11. Web Interface

### 11.1 REST API Endpoints

All endpoints served by `AsyncWebServer` on port 80.

| Method | Path | Response | Notes |
|--------|------|----------|-------|
| GET | `/api/state` | JSON `SystemState` (selected fields) | Current system snapshot |
| GET | `/api/history?h=24` | JSON array of `LogEntry` | h = 1, 6, 24, or 168 (7 days) |
| GET | `/api/alarms` | JSON array of active + recent alarms | |
| POST | `/api/cmd/defrost` | `{"ok":true}` | Trigger manual defrost |
| POST | `/api/cmd/alarm/clear` | `{"ok":true}` | Clear latched non-critical alarms |
| GET | `/api/config` | JSON of all runtime-adjustable params | |
| POST | `/api/config` | `{"ok":true}` or `{"error":"..."}` | Body: JSON with changed params |
| GET | `/api/ota` | JSON `{current, latest, available, installing}` | OTA status |
| GET\|POST | `/api/ota/check` | `{"ok":true}` | Force an immediate GitHub release check |
| GET\|POST | `/api/ota/install` | `{"ok":true,"status":"installing"}` / 409 | Self-update firmware from latest GitHub release (`?fs=1` also updates the filesystem); reboots on success |
| GET\|POST | `/api/ota/install-fs` | `{"ok":true,"status":"installing"}` | Filesystem-only update (`spiffs.bin` → SPIFFS); reboots on success |

`/api/ota` also returns `auto` (bool) — true when the latest release tag is an
auto-deploy tag (see §11.5).

**§11.6 Remote telemetry (v0.7+).** A low-priority task (`telemetry.cpp`) does a
fire-and-forget HTTPS POST of the status JSON (the same `status::buildJson` used
by `/api/state`, plus a `device` field) to a cloud endpoint every
`TELEMETRY_INTERVAL_MS` (30 s). **It must never block or affect the
control/safety loop** — lowest priority, bounded 8 s timeout, silent on failure,
and a full no-op unless configured. Config precedence mirrors WiFi: a runtime
config in SPIFFS (`/telemetry.json`, set via `POST /api/telemetry`) **overrides**
the compile-time `TELEMETRY_URL`/`TELEMETRY_TOKEN` in `config_secrets.h`. Auth is
a bearer token. The backend is a Cloudflare Worker + KV serving a public,
read-only viewer (see `cloud/`). TLS uses `setInsecure()` (same rationale as
OTA). **Caveat:** a token baked into a public release binary is extractable —
prefer the runtime config where local access exists; treat a baked ingest token
as low-value and rotatable.

**§11.5 Auto-deploy releases (v0.5+).** A release whose tag ends in lowercase
`a` (e.g. `v0.7a`) is **auto-installed** by eligible devices without any user
action: on the next GitHub check the device pulls firmware **and** filesystem
and reboots. The trailing `a` is ignored by the version comparison
(`isNewer()`), so `v0.7a` is "newer than v0.6". **Loop guard:** the last
auto-installed tag is persisted in NVS (`Preferences` namespace `ota`, key
`auto_tag`); a device never auto-installs the same tag twice, so it cannot boot
loop even if an auto build reports a wrong version string. Firmware failure
aborts (retries next check); filesystem is best-effort. Use auto-deploy for
fleet-wide rollouts; use a plain tag (no `a`) when you want a manual,
operator-confirmed update.
| GET | `/api/hardware` | JSON | Hardware status: OneWire/I²C scan + live input/analog reads (see §11.4) |
| GET\|POST | `/api/hardware/rescan` | `{"ok":true}` | Re-scan I²C/OneWire (performed by the LCD task to avoid bus races) |
| GET | `/api/telemetry` | JSON `{enabled,url,token_set,source,last_status,secs_since_ok}` | Telemetry status (token never returned) |
| POST | `/api/telemetry` | `{"ok":true}` | Set telemetry URL+token (form params `url`,`token`); stored in SPIFFS |
| GET | `/settings.html` | HTML | Settings page — live hardware status list (auto-refresh 3 s) |
| GET | `/update` | HTML | ElegantOTA firmware upload page |
| GET | `/ws` | WebSocket | 1 s push, see §11.2 |

**§11.4 Hardware status (`/api/hardware`).** Reports what is actually wired, for
bring-up. Auto-detected: DS18B20 count on the OneWire bus (GPIO4) and I²C device
presence (LCD 0x27/0x3F, SHT31 0x44/0x45). Live reads: digital input levels
(door 19, HP 34, LP 35, flow 39) and the SCT-013 ADC (36). Relay outputs K1–K5
are listed but **not driven** in Phase 0 (driving them could energise the
compressor/defrost relay; SSR polarity unconfirmed) — so they cannot be
sense-detected. Modbus devices are "Phase 1". Bus scans run at boot and on
rescan in the LCD task (it owns I²C); input reads happen live in the web task.

> **OTA & the filesystem (v0.4+):** firmware OTA flashes the **app partition**
> (dual-bank, safe). Web pages live in **SPIFFS** (single-bank). As of v0.4 the
> filesystem can also be updated over the air: `/api/ota/install` with `?fs=1`,
> or `/api/ota/install-fs`, pulls `spiffs.bin` from the release and flashes it
> (`U_SPIFFS`). Order: firmware first (inactive bank), filesystem last, then one
> reboot — so a firmware-download failure changes nothing, and a rare SPIFFS
> write failure is recoverable (`SPIFFS.begin(true)` auto-formats; re-push the
> filesystem). Releases must therefore include a `spiffs.bin` asset (see
> `RELEASING.md`). First-time/bootloader/partition-table changes still need USB.

**GitHub OTA security note:** the in-firmware updater uses `client.setInsecure()`
(no TLS cert validation) for the release check and download. Deliberate: pinning
a GitHub root cert would break OTA whenever GitHub rotates its roots, stranding a
remote device with no recovery but a physical reflash. The firmware is published
from a **public** repo and the local ElegantOTA `/update` path stays the trusted
channel. `check`/`install` accept GET so they can be triggered from a browser
address bar during bring-up. On a private repo, set `GITHUB_TOKEN` in
`config_secrets.h`; on public it stays empty.

### 11.2 WebSocket JSON Format

Pushed every 1 s to all connected clients. Fields with `null` indicate sensor error.

```json
{
  "ts": 1748700000,
  "mode": "DEEP_COOL",
  "T_kammer_mean": -29.4,
  "T_kammer": [-29.1, -29.4, -29.7],
  "T_kopf": 68.2,
  "T_saug": -19.8,
  "T_ww_oben": 58.1,
  "T_ww_unten": 52.3,
  "T_aussen": 22.4,
  "T_verd": -33.1,
  "T_bphe": 55.7,
  "T_whirlpool": 31.2,
  "RH_kammer": 81.0,
  "pv_w": 8800,
  "grid_w": -2100,
  "comp_w": 2100,
  "pv_day_kwh": 14.2,
  "pv_valid": true,
  "comp_running": true,
  "comp_on_s": 2820,
  "blower_on": true,
  "defrost_on": false,
  "bypass_open": false,
  "evi_open": true,
  "door_open": false,
  "hp_ok": true,
  "lp_ok": true,
  "ss_current_a": 21.4,
  "ss_valid": true,
  "alarm_flags": 0,
  "uptime_s": 86421,
  "fw": "v0.1"
}
```

### 11.3 Web Pages

| URL | File | Function |
|-----|------|----------|
| `/` | `index.html` | Live dashboard (WebSocket 1 s) |
| `/history` | `history.html` | Temperature + energy charts 24 h / 7 d |
| `/settings` | `settings.html` | Runtime parameter editor |
| `/alarms` | `alarms.html` | Active + historical alarm log |
| `/update` | ElegantOTA built-in | OTA firmware upload |

---

## 12. Open Items

| Status | ID | Description | Impact |
|--------|-----|-------------|--------|
| 🔴 BLOCKER | SS6 | **Soft starter ZSR4022 Modbus register map** — start/stop command register, current register, status/fault registers. Obtain from the „Communication protocol" chapter of the full manual. | Phase 1 compressor control |
| 🟡 IMPORTANT | E1 | **Bypass valve K4** — 230VAC or 24VDC solenoid? Determines SSR vs relay type for K4. | Phase 4 wiring |
| 🟡 IMPORTANT | E2 | **Defrost heater Kevely** — voltage (230V or 400V 3-ph) and wattage | Contactor sizing for K3 |
| 🟡 IMPORTANT | E3 | **Blower fans** — 230V or 400V 3-phase? | Relay vs contactor for K2 |
| 🟡 IMPORTANT | E4 | **WW pump + pool pump** — ESP32-controlled (each +1 relay) or external/manual? | Relay count |
| 🟡 IMPORTANT | PV4 | **Deye RS485 physical port location** — which connector on the device? Not the meter port (addr 2). | Phase 3 wiring |
| 🟡 IMPORTANT | PV5 | **Active MPPT strings on Deye** — 1 or 2? → PV1+PV2 vs PV1 only | Data accuracy |
| 🟢 INFO | N1 | **WiFi SSID + password on Samosir** | Phase 0 WiFiManager |
| 🟢 INFO | N2 | **GitHub repository URL** — public or private? | Phase 0 OTA config |
| 🟢 INFO | N3 | **Internet connectivity on Samosir** — stable enough for GitHub OTA? | OTA strategy |
| 🟢 INFO | B1 | **Daily product throughput** — how much fresh warm ice enters per day? | Puffer-Prognose accuracy |
| 🟢 INFO | B3 | **Copeland FLA** — read from compressor nameplate → set in soft starter | Commissioning |
| ✅ DONE | PV3 | Deye BM2: no battery, on-grid only | Simplifies energy logic |

---

## 13. PlatformIO Configuration

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = spiffs
board_build.partitions = partitions_ota.csv

lib_deps =
  esphome/ESPAsyncWebServer-esphome
  AsyncTCP
  ayushsharma82/ElegantOTA
  bblanchon/ArduinoJson@^7
  marcoschwartz/LiquidCrystal_I2C
  paulstoffregen/OneWire
  milesburton/DallasTemperature
  closedcube/ClosedCube SHT31D
  4-20ma/ModbusMaster
  ESP32Time

[env:esp32dev_ota]
extends = env:esp32dev
upload_protocol = espota
upload_port = 192.168.x.x
upload_flags = --auth=kk-samosir

[env:native]
platform = native
build_flags = -std=c++17
; Hardware stubs compiled in for tests — see test/stubs/
```

### OTA Partition Table (`partitions_ota.csv`)

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
app0,     app,  ota_0,   0x10000,  0x1C0000
app1,     app,  ota_1,   0x1D0000, 0x1C0000
spiffs,   data, spiffs,  0x390000, 0x70000
```

---

## 14. Solar Forecast Model

Used when Deye Modbus is unavailable (A14 active) or as a planning tool.

```cpp
// Samosir Island, Lake Toba
const float LAT       = 2.6167f;    // °N
const float LON       = 98.8333f;   // °E (UTC+7)
const float PV_PEAK_W = 12000.0f;
const float ETA       = 0.80f;      // system efficiency incl. inverter losses

// Returns 0 before sunrise and after sunset.
// Returns PV_PEAK_W × sin(elevation_fraction) × ETA at solar noon.
// Does NOT model cloud cover.
float calc_solar_watt(int hour, int minute, int day_of_year);
```

---

*Architecture v0.1 | Kältekammer Samosir | Stand: Juni 2026*
