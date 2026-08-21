# CLAUDE.md — UV Illuminator Firmware

## Project scope

This project is the **production firmware for the UV-illuminator application** of the shared
laser-controller rev_1 PCB (CN-19: general-purpose platform, configurable per application without
PCB redesign). It is one of several application firmwares built on this board; the hardware
bring-up/test firmware lives in the sibling project `../hw-cli` and must not be modified from here.

Main control surfaces for this application:
- **Direct IO**: three potentiometers (power, duty cycle, frequency), one momentary push button
  (UV on/off), ARM_GATE safety output enable, TTL pulse + DAC power-level outputs, one feedback LED.
- **16×2 character LCD** (4-bit parallel) for status/parameter display.
- **Debug serial (USART2 / `Serial2`, PF0/PF1, 115200 8N1) is diagnostic log output only** —
  there is no CLI on this firmware. Do not build a command parser here.

---

## Target hardware

- **MCU:** Microchip AVR128DB48 (TQFP-48), 24 MHz internal oscillator
- **PCB:** Laser Controller rev_1 (schematic: `../../../implementation/Schematic.pdf`)
- **Board family pin map:** see `pins.md` in this directory (UV-illuminator subset) and
  `../hw-cli/pins.md` (full shared-board map) for anything not listed here.

---

## Pin assignments

- **`pins.md` is the single source of truth for pin assignments used by this application.**
- Read `pins.md` before writing any peripheral initialisation code.
- `pins.md` is maintained by the user — do not overwrite or reorder it. Only append or update
  entries the user explicitly asks to change.
- If a pin is marked `TBD` or `❌`, do not hardcode it. Write a `// TODO: confirm pin` comment.
- This application uses only a subset of the shared board's I/O — do not add drivers for pins
  listed as "not used in this application" without explicit instruction.
- `include/pins.h` mirrors `pins.md` in code — named constants only, no peripheral logic there.

---

## Architecture — linear state machine, no RTOS

**Decision: a straightforward superloop with a top-level state enum, driven by non-blocking
`millis()`-based scheduling. No RTOS.**

Why:
- Behaviour is inherently sequential and small: idle → armed → illuminating → fault, with a
  handful of periodic housekeeping tasks (pot sampling, LCD refresh, heartbeat blink, fault/temp
  polling). None of this needs preemptive multitasking.
- ARM_GATE is a safety-relevant output. A single deterministic loop makes worst-case response
  latency easy to reason about (SR-23 requires display fault indication within 500 ms); an RTOS
  adds scheduling jitter and a failure mode (task starvation, priority inversion) that buys nothing
  here.
- AVR128DB48 has 16 KB RAM. An RTOS's per-task stacks are a real cost on this part for no
  behavioural benefit at this complexity level.
- This pattern is the standard, well-understood approach for this class of device and is easy for
  future maintainers (including non-RTOS-experienced firmware engineers) to follow.

Structure:
- `enum class State { INIT, SAFE, ARMED_IDLE, ILLUMINATING, FAULT }` drives a single `switch` in
  `loop()`. State transitions are explicit and logged over `Serial2`.
- Periodic tasks (pot sampling, LCD refresh, heartbeat, fault/temp poll) are each gated by their
  own `millis()` interval check — no `delay()` anywhere in `loop()`.
- ARM_GATE is only ever driven HIGH from the `ARMED_IDLE`/`ILLUMINATING` states; any fault or
  button-press-to-disarm transition drives it LOW/Hi-Z immediately, ahead of any display or log update.

Revisit this decision only if the application grows genuinely concurrent requirements (e.g.
multiple independent timing-critical channels that can't share one loop cleanly) — not expected
for this application.

---

## Fault chain (multi-board fault topology)

**Decision (2026-07-30): one unified fault path, not build-time-exclusive roles.** See `pins.md`
"System context" / "Fault chain" for the system-level explanation and hardware confirmation.

Every unit, every build:
- Reads an active-LOW fault-in signal on PB4 (`PIN_FAULT_IN`, J18) from the upstream board,
  `INPUT_PULLUP` (no external pull-down on this board's population — see `pins.md`).
- Drives an active-LOW fault-out signal on PB5 (`PIN_FAULT_OUT`, J19) to the downstream board —
  HIGH = no fault, LOW = fault — updated every `loop()` iteration, immediately after the fault
  check and ahead of any state/display/log update (same ordering guarantee as ARM_GATE).
- Fault = `PIN_FAULT_IN` LOW **OR** (if `HAS_TEMP_SENSOR`) own DS18S20 reading over the
  over-temp limit. Either condition both latches this unit into `FAULT` and propagates LOW out
  `PIN_FAULT_OUT`, so a fault anywhere in the chain cascades downstream.

The only thing that still varies per physical unit is whether a DS18S20 is fitted — select via
`HAS_TEMP_SENSOR` in `include/pins.h` (defaults to 1). PB4/PB5 are unconditional; there is no
longer a "fault-input-only" build without them.

**Superseded (two rounds):** this firmware previously used a `BOARD_ROLE` flag
(`BOARD_ROLE_TEMP_SENSOR` vs. `BOARD_ROLE_FAULT_INPUT`) to select *either* the DS18S20 *or* an
incoming fault-in on PF4, mutually exclusive per build. Retired because it couldn't express "read
my own sensor if I have one, and also relay whatever's coming from upstream" — every unit needs
the relay behavior regardless of sensor population. PF4 was also confirmed by the user to be the
wrong pin for fault-in on this hardware rev; the first correction moved it to PD5, which itself
turned out not to work as an input ("the circuit can't do active high inputs," per the user) — PB4
is the pin that does. See `pins.md` "Fault chain" for the full pin history.

---

## Known hardware gotchas (Curiosity Nano DB board package)

- `LED_BUILTIN` (`#define`d to `PIN_PB3`) must never be used — PB3 is ARM_GATE on this PCB.
- The default `Serial` object (`#define`d to `Serial3`, which DxCore maps to PB0/PB1) must never
  be used — PB0/PB1 are TTL_PULSE_OUT/SYNC_OUT on this PCB. Always use `Serial2` explicitly for
  debug logging.

---

## Libraries

| Library | Purpose |
|---|---|
| DxCore (built-in) | AVR128DB48 peripherals: TCA0, DAC0, USART, OneWire pin access |
| LiquidCrystal (Arduino built-in, 4-bit mode) | 16×2 character LCD driver |
| OneWire | 1-Wire bus for DS18S20 (`HAS_TEMP_SENSOR` builds only) |
| DallasTemperature | DS18S20 temperature conversion (`HAS_TEMP_SENSOR` builds only) |

Do not add Modbus, RS-485, or OLED/I2C libraries — not used by this application (see pins.md).
Prefer direct register access for TCA0/DAC0 over Arduino wrapper calls where DxCore's Arduino API
doesn't cleanly expose what's needed (e.g. PWM frequency below Arduino's default `analogWrite`
range, or DAC0 VREF selection).

---

## Session log

- Maintain **`session_log.md`** in this directory, same format as `../hw-cli`.
- At the start of every session, read it in full before doing anything else.
- Append a dated entry at the end of every session: what was done, decisions made and why, pin
  assignments confirmed/updated, open questions, and what to do next. Never truncate past entries.

---

## Out of scope — do not implement

- Serial CLI / command parser (debug port is log output only)
- Modbus / RS-485 protocol stack
- Interlock loop logic (confirmed not used on this application)
- OLED/I2C display driver (LCD only on this build)
- 8-bit digital power-level output, external DAC8551, external trigger input (not used — see pins.md)
- RTOS/task-based scheduling (see Architecture decision above)

## Open questions

- Fault LED (PE1): confirmed **not installed** on this application. Do not wire it into
  firmware logic. On-board heartbeat LED (PE3) is confirmed installed and implemented with
  state-dependent blink patterns (see `src/main.cpp` `PATTERN_*` tables / `updateHeartbeat()`).
