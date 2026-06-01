# Kältekammer ESP32 — Development Process

This file defines the mandatory process for every development session.  
**Follow phases in order. Do not skip or merge them.**

10,000 kg of ice cream are on the line. The safety layer and control logic must be correct. No shortcuts.

---

## Phase 0 — Orientation (start of every session)

1. Read this file (`DEVELOPMENT.md`) in full.
2. Read `ARCHITECTURE.md` to get the current state of the firmware: module layout, data model, Modbus register maps, open items, and known bugs.
3. Run `git log --oneline -10` to see recent commits and understand what has already been done.
4. Check the open items table in `ARCHITECTURE.md §12`. Note any items that are now resolved or that block the current session's work.
5. If the user references a previous plan or open issue, ask them to summarise it before proceeding.

Only move to Phase 1 once you have a solid mental model of the current firmware state.

---

## Phase 1 — Requirements & Planning

**Goal:** Eliminate ambiguity before writing a single line of code.

Ask clarifying questions until all of the following are answered:

- **Intent**: What problem does this change solve? What is the desired outcome?
- **Scope**: What is explicitly in scope? What is explicitly out of scope?
- **Safety implications**: Does this change touch the safety layer, compressor control, defrost sequencing, or alarm handling? If yes, flag immediately — these require extra test coverage and a dedicated safety review pass.
- **Hardware dependencies**: Does this require new sensors, actuators, or Modbus registers? Are those registers verified on the physical device, or are they from documentation only?
- **State machine impact**: Does this change affect operating mode transitions, the defrost state machine, or EVI control? If yes, map out all affected transitions.
- **SPIFFS/config impact**: Does this add new configuration parameters? Do existing stored configs need migration?
- **Web interface**: Does this require new API endpoints or WebSocket message fields? What are the loading, empty, and error states?
- **Edge cases**: What happens when a sensor reads an error value (`-127°C` for DS18B20), when Modbus times out, when the door is opened mid-defrost, when PV data is stale?
- **Success criteria**: How will we know this is working correctly? What does a passing test look like on the bench, and on the installed system?

Do not assume. Do not default to the simplest interpretation without confirming.  
Questions are cheap. A burned-out compressor or 10 tons of spoiled ice cream is not.

---

## Phase 2 — Implementation Plan

Write a short, structured plan before touching any file:

1. **Summary** — one paragraph: what will be built and why.
2. **Files to create** — new `.h` / `.cpp` / `.html` files and their purpose.
3. **Files to change** — existing files and exactly what changes.
4. **SystemState changes** — any new fields added to the central state struct.
5. **New config parameters** — anything added to `config.h` or persisted in SPIFFS.
6. **New API/WebSocket changes** — new REST endpoints or new fields in the WebSocket JSON.
7. **Tests to write** — every new pure function that contains business logic must have unit tests. List them explicitly: function name + key test cases (happy path, boundary, failure).
8. **Safety review flag** — explicitly state whether this change touches safety-critical code.
9. **Out of scope** — what is explicitly not being done in this session.

Present the plan to the user. Do not begin implementation until the user has approved it.  
If the user asks for changes, revise the plan and re-present.

---

## Phase 3 — Execution

Implement the approved plan. Rules:

### Testing
- **Tests are written alongside or before the implementation** — not after.
- Every new pure function that contains business logic gets a unit test in `test/`.
- Use the PlatformIO Unity framework with a `[env:native]` target so tests run on the host machine without hardware.
- All hardware-dependent calls (`digitalRead`, `analogRead`, Modbus calls, OneWire) must be abstracted behind a thin interface or accessed only through `SystemState` — never called directly from business logic. This is what makes logic testable without hardware.
- **Safety-critical functions require failure-case tests**, not just happy-path tests. See the mandatory test list in `ARCHITECTURE.md §9`.

### Code rules
- No magic numbers. Every threshold, timeout, and constant lives in `config.h` with a name.
- No global mutable state outside of `SystemState`. Tasks communicate through the shared state struct, protected by a mutex.
- No `delay()` in any task. Use `vTaskDelay()` with `pdMS_TO_TICKS()`.
- No blocking Modbus calls in the safety task. Modbus runs in its own task; the safety task reads from `SystemState` only.
- All Modbus operations include a timeout and an error counter. Three consecutive failures → fallback mode + alarm, never a hang.
- All DS18B20 reads validate the result: values outside `-60°C` to `+150°C` are treated as sensor errors, not used for control decisions.
- No `Serial.print` in production paths — use the structured log buffer. `Serial` is fine during Phase 0 bootstrap only.

### After each logical chunk (data layer, one task, one HTML page):
- Does it compile? (`pio run -e esp32dev`)
- Do the tests pass? (`pio test -e native`)
- Does it follow the patterns in `ARCHITECTURE.md`?

---

## Phase 4 — Safety & Logic Review

**Switch role: you are now the Safety Engineer reviewing every changed file.**

This phase is mandatory whenever a change touches any of the following:
- The safety task or alarm flags
- Compressor start/stop logic
- Minimum on-time / minimum off-time enforcement
- EVI valve control
- Defrost sequence state machine
- Operating mode determination
- Any threshold in `config.h` that affects when the compressor runs or stops
- Modbus communication with the soft starter

For every changed file in scope, verify:

**Compressor protection**
- [ ] Minimum off-time (`MIN_STANDZEIT` ≥ 180 s) is enforced before every start command — no path bypasses it.
- [ ] Minimum on-time (`MIN_LAUFZEIT` ≥ 300 s) is checked before every stop command (except safety stops — those are immediate and unconditional).
- [ ] Safety stops (A01 HP, A02 HP-switch, A03 LP-switch, T4 > 115°C) are immediate, cannot be blocked by min-on-time logic.
- [ ] After a safety stop, the compressor cannot restart until the alarm is cleared manually or the fault condition resolves below the reset threshold.

**Alarm handling**
- [ ] Every new alarm code is added to the alarm flag bitmask in `SystemState`.
- [ ] Every alarm that triggers a compressor stop also sets `comp_running = false` atomically.
- [ ] Alarms are latched — a momentary fault does not silently clear itself. Clearing requires explicit logic (condition gone + minimum hold time or manual reset).

**Sensor failure handling**
- [ ] A sensor error value does not substitute into a control decision. The fallback behaviour when a critical sensor fails is explicitly defined (e.g. T_kammer error → switch to ALARM mode, not continue with a bogus temperature).
- [ ] T4 (compressor head) failure means the compressor must not run — no temperature data means no thermal protection.
- [ ] The Modbus fallback (A15: SS RS485 failure) correctly activates the relay fallback K1 and logs the alarm.

**EVI valve**
- [ ] EVI can only open when `comp_on_s > 30` — never at startup.
- [ ] EVI closes unconditionally when the compressor stops — no path leaves it open with the compressor off.
- [ ] Superheat sanity check: EVI does not open if superheat < 8 K (liquid slugging risk).

**Defrost**
- [ ] Defrost cannot start during the night block window (22:00–06:00) except for emergency defrost.
- [ ] Defrost has a hard timeout (35 min) — it does not run indefinitely if `T_verd > +5°C` is never reached.
- [ ] The compressor cannot restart during the drip time (2 min post-heater-off).
- [ ] If the door opens during defrost, defrost pauses or aborts safely.

Write a bullet-point finding list:
- `[CRITICAL]` — a path that could damage the compressor or spoil product; blocks implementation
- `[must fix]` — a correctness or safety issue; blocks deploy
- `[should fix]` — a quality issue; fix before deploy
- `[consider]` — a suggestion; user decides

Implement all `[CRITICAL]` and `[must fix]` items. Do not move to Phase 5 until they are resolved.

---

## Phase 5 — Code Review

**Switch role: you are now a Senior Embedded Engineer reviewing a pull request.**

**Start here:** List every file changed in this session. Go through each one in turn.

- **Correctness**: Does the logic match the spec exactly? Off-by-one in a hysteresis check or a sign error in a Modbus register is a real bug here.
- **Concurrency**: Is every access to `SystemState` from multiple tasks protected by the mutex? Are there any race conditions between the safety task (1 s) and the control task (10 s)?
- **Memory**: No heap allocation in tasks (`malloc`, `new`, `String` concatenation) after setup. Use fixed-size buffers. Check stack sizes for each task.
- **Modbus robustness**: Is the response buffer index correct? Are signed registers cast to `int16_t` before use (critical for Deye grid power sign)?
- **OneWire addressing**: Are sensor addresses looked up by stored index, not by position on the bus (bus order can change after power cycle)?
- **WebSocket JSON**: Is the JSON serialised with `ArduinoJson`? No manual string concatenation for JSON.
- **SPIFFS**: Are file operations wrapped in existence checks? Is SPIFFS mounted before any read/write?
- **Architecture doc**: Are all new modules, tasks, API endpoints, config parameters, and SystemState fields reflected in `ARCHITECTURE.md`? This is a deploy blocker.

Write a bullet-point review with `[must fix]` / `[should fix]` / `[consider]` tags.  
Implement all `[must fix]` and `[should fix]` items. Present `[consider]` items to the user.

---

## Phase 6 — Test Verification

**Switch role: you are now the QA Engineer.**

**Automated tests**
- [ ] `pio test -e native` passes with zero failures.
- [ ] Every function listed in the mandatory test inventory (`ARCHITECTURE.md §9`) has tests.
- [ ] Boundary values are tested (exactly at threshold, one step below, one step above).
- [ ] Sensor error values are tested as inputs to every control function that reads temperature.
- [ ] No test uses `delay()` or real-time waits — all timing is injected via a clock parameter or fake timestamp.

**Build verification**
- [ ] `pio run -e esp32dev` compiles with zero errors and zero warnings.
- [ ] Firmware binary size fits within the OTA partition (`app0` / `app1`: 1.75 MB each). Check: `pio run -e esp32dev -t size`.

**Bench test checklist** (to be performed on hardware before installation):
- [ ] WiFiManager AP appears on first boot; credentials saved to SPIFFS; reconnects on reboot.
- [ ] ElegantOTA `/update` page accepts a `.bin` and flashes correctly; device reboots into new firmware.
- [ ] LCD shows correct startup screen; switches to sensor data after Phase 1 sensors are connected.
- [ ] WebSocket dashboard updates at 1 s interval; all mock/real values appear in the correct fields.
- [ ] Simulated HP alarm (pull P1 pin low) → compressor relay opens within 1 s + A02 flag set.
- [ ] Simulated LP alarm → same as above with A03.
- [ ] Modbus write to soft starter: verify start command sent, status register read back.
- [ ] Deye Modbus: verify PV1+PV2 registers read correctly (cross-check against inverter display).
- [ ] Defrost sequence: trigger manually via web, verify full sequence timing with a stopwatch.
- [ ] Min-off-time: attempt compressor start within 180 s of stop → start must be refused.
- [ ] Min-on-time: attempt stop within 300 s of start (without safety condition) → stop must be refused.

**If any bench test fails:** fix the root cause before marking the session complete. Do not ship firmware that fails a safety test.

---

## Phase 7 — Deploy (Flash)

Only proceed once Phases 4, 5, and 6 are fully complete.

1. **Update `ARCHITECTURE.md`** — mandatory. Every changed module, new config parameter, new API endpoint, new SystemState field, and resolved open item must be reflected before flashing.
2. Run `git status` — confirm no unintended files are staged. Confirm `config_secrets.h` is NOT staged (it is in `.gitignore`).
3. Commit all changes (including updated `ARCHITECTURE.md`) with a clear message.
4. Flash via USB for the initial production flash: `pio run -e esp32dev -t upload`.
5. For subsequent updates: use ElegantOTA (`/update`) or GitHub Releases OTA from the dashboard.
6. After flashing, verify the serial log shows clean startup (no crash, no SPIFFS mount error, WiFi connects).
7. Verify the web dashboard is reachable and shows live data.
8. On first production installation: run the full bench test checklist from Phase 6 on-site before leaving the system unattended.

---

## Standing Rules

These apply at all times, regardless of phase:

- **Never bypass the safety task.** The safety task runs at priority 5 with a 1 s interval. Nothing overrides it except a hard reset.
- **Never test with the real compressor connected during software development.** Use relay state logging without energised hardware until the control logic is fully verified.
- **Never use `String` (Arduino heap string) in a FreeRTOS task.** Use `char[]` or `snprintf` into fixed buffers.
- **Never store credentials in the repo.** `config_secrets.h` is in `.gitignore`. If it ever appears in `git status` as tracked, stop and fix the `.gitignore` immediately.
- **Never write to SPIFFS from an ISR or the safety task.** SPIFFS writes are slow and non-reentrant.
- **Modbus register addresses from documentation must be verified against the physical device** before any code that depends on them is considered production-ready. Mark unverified registers clearly in `ARCHITECTURE.md §12`.
- **All open items in `ARCHITECTURE.md §12` must be checked at the start of every session.** A blocker that was resolved offline is not reflected in the code until someone notes it.
- **The web dashboard is informational, not authoritative.** The ESP32 must operate safely with no browser connected. The control loop must not depend on WebSocket state.
