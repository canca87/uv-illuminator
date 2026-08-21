# pins.md — MCU Pin Assignments (UV Illuminator application)

MCU: **AVR128DB48** (TQFP-48)
Schematic: `../../../implementation/Schematic.pdf` (rev_1, sheet 2 of 3 — MCU) — shared board, see also
`../../hw-cli/pins.md` (full board pin map for this same PCB, hw-cli test firmware).
Source files: Altium — PDF schematic only, no raw netlist available.

This board is a general-purpose laser-controller platform (CN-19) populated differently per
application. **This file lists only the nets/pins used by the UV-illuminator application.**
Pins present on the board but not used here are listed at the bottom for reference, so nobody
accidentally repurposes a net that's already spoken for elsewhere in the system.

> **This file is maintained by the user.** Claude Code must not overwrite or reorder entries.
> Update only what the user explicitly asks to change.

> **Status key:**
> - ✅ Confirmed — verified from schematic, hardware, or explicit user statement this session
> - 🔶 Inferred — deduced from AVR128DB48/DxCore constraints or cross-checked against
>   `../../hw-cli/pins.md`; confirm before final sign-off
> - ❌ TBD — unknown; do not hardcode until the user fills this in

---

## System context — multi-board fault topology

This UV illuminator is one of **four boards** in the larger system (same PCB, different
firmware/population per role). As of 2026-07-30 (see `session_log.md`), every unit runs the same
unified fault logic:

- Every unit reads an upstream active-LOW fault-in signal on **PB4 (J18)**.
- Every unit drives its own active-LOW fault-out signal on **PB5 (J19)** — HIGH = no fault, LOW =
  fault — daisy-chained into the next unit's PB4.
- Two of the four units additionally have a DS18S20 fitted and read their own over-temperature
  directly via 1-Wire (PF2); the other two don't.
- Fault state is the OR of "upstream FAULT_IN is LOW" and "own over-temp, if a sensor is fitted."
  Any true condition both latches this unit's own state machine into FAULT **and** drives its
  FAULT_OUT low, so the condition propagates down the chain.

**Only the temperature-sensor population differs per physical unit**, selected at compile time
via `HAS_TEMP_SENSOR` in `include/pins.h`. The fault in/out pins are unconditional — every build
of this firmware reads PB4 and drives PB5.

**Superseded (two rounds):** PF4 (J23) was first documented as the fault-in net (selected
exclusively against the temp-sensor role via a now-retired `BOARD_ROLE` flag). Corrected on
2026-07-30 to PD5 (J17). Corrected again the same day to the current pin, **PB4 (J18)** — PD5
turned out not to work as this input: per the user, "the circuit can't do active high inputs,"
and PB4 is the pin that does. FAULT_IN's electrical treatment on PB4 is the same as the earlier
PD5/PF4 attempts — active-LOW with the AVR's internal pull-up enabled (`INPUT_PULLUP`); per the
user, there is **no external pull-down** on this board's PB4 net (unlike the pull-down on
`EXT_TRIG_IN`/PB4 documented in `../../hw-cli/pins.md` for that project's board population — do
not assume that resistor is populated here). Both PF4 and PD5 entries are kept below under
"Present on shared board — NOT used" rather than deleted, since the schematic nets still exist
even though this firmware no longer reads them. See `session_log.md` for the full history.

---

## Output — UV source control (default build)

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 4 | PB0 | TTL_PULSE_OUT | Output | TCA0 WO0 — on/off pulse control for UV source | ✅ |
| 26 | PD6 | DAC_OUT | Output | DAC0 (10-bit, fixed silicon pin) — sets UV power level | ✅ |

TCA0 WO0 default-maps to PB0 with no PORTMUX change needed (confirmed against DxCore
`48pin-standard` variant). DAC0 output range depends on VREF selection (configure via VREF
peripheral). Both pass through the ARM gate before reaching the output connector.

---

## ARM Gate — output enable (safety)

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 7 | PB3 | ARM_GATE | Output | Push-pull GPIO — drive HIGH to enable outputs, LOW/Hi-Z to inhibit | ✅ |

Two-FET topology (see `../../hw-cli/pins.md` for full detail): MCU drives **HIGH** to arm,
**LOW / Hi-Z** is the fail-safe inhibited state. At reset, GPIO defaults to input (Hi-Z) → safe
state is automatic; explicitly drive LOW in early startup to confirm it before ever driving HIGH.

**No interlock loop (PF5) on this application** — confirmed not used. Do not wire INTERLOCK
logic into the ARM_GATE path for this build.

⚠️ **Gotcha:** the Curiosity Nano DB board package defines `LED_BUILTIN` as `PIN_PB3`. That macro
must **not** be used in this firmware — PB3 is ARM_GATE on this PCB, not an LED.

---

## LCD — 16×2 character display, 4-bit parallel (CN-11)

| Pin | Port | Net (shared board) | Direction | LCD signal | Status |
|---|---|---|---|---|---|
| 46 | PA2 | OLED_SDA_LCD_RS | Output | RS | ✅ |
| 47 | PA3 | OLED_SCL_LCD_E | Output | E | ✅ |
| 48 | PA4 | LCD_DB4_SPI_MOSI | Output | DB4 | ✅ |
| 1 | PA5 | LCD_DB5_SPI_MISO | Output | DB5 | ✅ |
| 2 | PA6 | LCD_DB6_SPI_SCK | Output | DB6 | ✅ |
| 3 | PA7 | LCD_DB7_SPI_SS | Output | DB7 | ✅ |

This is the LCD-parallel alternative population of the dual-purpose PA2–PA7 pins documented in
`../../hw-cli/pins.md` (that project instead fits the OLED/I2C alternative — never both). No R/W
line is listed on either alternative; 🔶 **inferred** that R/W is tied to GND on the PCB
(write-only display, standard practice) — confirm against schematic before relying on this.

---

## Manual controls (CN-05)

| Pin | Port | Net | Direction | ADC/GPIO name | Use | Status |
|---|---|---|---|---|---|---|
| 20 | PD0 | POT_PWR | Input | `pwr` | UV power-level setpoint | ✅ |
| 21 | PD1 | POT_DC | Input | `dc` | Duty cycle setpoint | ✅ |
| 22 | PD2 | POT_FREQ | Input | `freq` | Pulse frequency setpoint | ✅ |
| 37 | PF3 | MODE_SEL (repurposed) | Input | — | Momentary push button, active-LOW, pull-up on PCB (J21) — UV on/off | ✅ |

POT_PHASE (PD3) present on the shared board — **not used** in this application.
Pushbutton reuses the MODE_SEL net/pin from the base board design; on this application it's wired
to a momentary switch rather than a maintained mode-select switch, but the electrical
characteristics (active-low, PCB pull-up) are the same.

---

## Temperature sensor — optional population

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 36 | PF2 | ONE_WIRE | Bidir | DS18S20 1-Wire, pull-up on PCB (4.7 kΩ) to +5V — only on units with a sensor fitted | ✅ |

Fitted on two of the four units in the system; selected at compile time via `HAS_TEMP_SENSOR` in
`include/pins.h`. See "Fault chain" below for how this combines with FAULT_IN/FAULT_OUT.

---

## Fault chain — every unit

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 8 | PB4 | EXT_TRIG_IN (repurposed → FAULT_IN) | Input | Active-LOW TTL fault-in from upstream board's PB5 fault-out (J18); internal `INPUT_PULLUP`, no external pull-down on this board's population | ✅ |
| 9 | PB5 | SYNC_IN (repurposed → FAULT_OUT) | Output | Active-LOW TTL fault-out to downstream board's PB4/FAULT_IN (J19); HIGH = no fault | ✅ |

Unconditional on every build — no compile-time selection. Fault state is FAULT_IN LOW **OR** own
over-temp (if `HAS_TEMP_SENSOR`); either condition drives FAULT_OUT low. Confirmed by user
2026-07-30 (see `session_log.md`).

⚠️ **Net-name overload:** the shared board's `../../hw-cli/pins.md` documents PB5 as `SYNC_IN`
(an *input*, pulled down). On this application it's repurposed as an *output* (FAULT_OUT) —
standard "populated differently per application" pattern for this board family, not a conflict.

---

## Feedback LED

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 30 | PE0 | LED_LASER_ACTIVE (repurposed → UV_ACTIVE_LED) | Output | UV on/off state feedback to operator (J22) | ✅ |

Off-board, panel-mounted, active-LOW (per SR-37 convention on this board family).

---

## On-board heartbeat LED

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 33 | PE3 | LED_HEARTBEAT | Output | State-dependent blink pattern — see `src/main.cpp` `PATTERN_*` tables | ✅ |

Confirmed installed. Blinks a distinct, human-visible pattern per operating state (fast
triple-blink on init, single slow pulse when SAFE, continuous fast blink when ILLUMINATING,
urgent double-blink on FAULT) rather than a plain fixed-rate blink, so the state is readable at a
glance without the LCD.

---

## Debug UART (J8 programming header) — debug/logging only

| Pin | Port | Net | Direction | Use | Status |
|---|---|---|---|---|---|
| 34 | PF0 | DB-TX | Output | USART2 (`Serial2`) TXD — 115200 8N1 debug log output | ✅ |
| 35 | PF1 | DB-RX | Input | USART2 (`Serial2`) RXD — 115200 8N1 (not expected to be used, but wired) | ✅ |

This application has **no serial CLI** — the debug port is diagnostic log output only. Main
control is via direct IO (pots, button, ARM_GATE, pulse/DAC) and the LCD.

⚠️ **Gotcha:** the Curiosity Nano DB board package `#define`s the default `Serial` object to
`Serial3`, which DxCore maps to **PB0/PB1 by default** — those pins are TTL_PULSE_OUT/SYNC_OUT on
this PCB, not a serial header. **Always use `Serial2` explicitly**; never use the bare `Serial`
object in this firmware.

---

## Power and programming

| Pin | Port | Net | Notes |
|---|---|---|---|
| 41 | UPDI | UPDI | Programming/debug via J8 header |
| 42 | VDD | +5V_IN | 5 V logic supply |
| 28 | AVDD | +5V_IN | Analog supply (tied to VDD on this board) |
| 14 | VDDIO2 | +5V_IN | PORTC I/O supply |
| 15, 29, 43 | GND | GND | Ground |

---

## Present on shared board — NOT used in this application

For reference only, so these nets aren't accidentally repurposed. Full detail in
`../../hw-cli/pins.md`.

| Net | Pin(s) | Reason not used |
|---|---|---|
| INTERLOCK | PF5 | Confirmed: no interlock loop on this application |
| SYNC_OUT | PB1 | Not used — TCA0 WO1 left unconfigured |
| POT_PHASE | PD3 | Only 3 of 4 pots used (pwr, dc, freq) |
| EXT_ANALOG_MON | PD4 | Not used |
| LED_FAULT | PE1 | Not installed on this application — confirmed |
| LED_INTERLOCK | PE2 | Not applicable — no interlock on this build |
| 8-bit digital output (DIG_D0-7) | PC0-PC7 | Not used — power level set via DAC only |
| SPI0 (external DAC8551 / n/a here) | PA4-PA7 | Superseded by LCD use on these pins |
| RS-485 / FTDI | PA0, PA1, PB2 | Confirmed: serial port is debug-only, no RS-485/Modbus |
| LED_FAULT | PE1 | Fault LED not installed on this application (confirmed) |
| LASER_FAULT_IN (FAULT_IN) | PF4 (J23) | **Deprecated 2026-07-30** — first-round FAULT_IN candidate, superseded same day; see "Fault chain" section and `session_log.md`. Net still exists on the schematic, just not read by this firmware. |
| FB_IN_ADC (FAULT_IN) | PD5 (J17) | **Deprecated 2026-07-30** — second-round FAULT_IN candidate; per the user, "the circuit can't do active high inputs" on this pin. Superseded by PB4/J18 the same day. |
