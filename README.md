# UV Illuminator Firmware — State Machine

Operator-facing reference for the four states in [src/main.cpp](src/main.cpp). See
[CLAUDE.md](CLAUDE.md) for the architecture rationale and [pins.md](pins.md) for pin assignments.

The **heartbeat LED** (PE3, on-board) is the fastest way to confirm state without reading the
display, since each pattern is visually distinct and it's the only indicator that distinguishes
all four states. The **LCD** (16×2, CN-11) top line only distinguishes `FAULT` from everything
else — it shows the live pulse frequency in every other state (see below) — so on its own it
can't tell `SAFE` and `ILLUMINATING` apart.

State is also propagated off-board: every unit reads an active-LOW fault-in on PB4 (J18) from
the upstream board and drives its own active-LOW fault-out on PB5 (J19, `FAULT_OUT`) to the
downstream board — HIGH = no fault, LOW = fault. This is independent of, and updated ahead of,
the heartbeat/LCD/log — see "Fault chain" in `pins.md` and `CLAUDE.md`.

**`Serial2` debug log lines (quoted throughout this doc) only exist in development builds.**
The default PlatformIO environment, `curiosity_nano_db`, compiles them out entirely
(`ENABLE_DEBUG_LOG=0`, see `include/debug_log.h`) so production firmware doesn't pay the
per-`loop()` cost of blocking USART writes. Build/upload with `curiosity_nano_db_debug` instead
(`pio run -e curiosity_nano_db_debug -t upload`) to get `Serial2` output back.

---

## INIT

**Entered:** at power-on / reset, before `setup()` completes.

**Requirements to leave:** none — this is transient. `setup()` unconditionally drives
`ARM_GATE` low, initializes the LCD and pulse timer, then transitions straight to `SAFE`. The
firmware never re-enters `INIT` after startup.

**Observe:**
- Heartbeat (PE3): fast triple-blink, then a longer pause (`{100,100,100,100,100,500}` ms) —
  visible only briefly during boot.
- LCD: not guaranteed to show `INIT` — the transition to `SAFE` happens before the first LCD
  refresh in most cases.
- `Serial2` (115200 8N1, debug log only): `"UV-illuminator firmware starting"` followed by
  `"-> SAFE"`.
- `ARM_GATE` (PB3): held LOW throughout.
- `FAULT_OUT` (PB5): initialized HIGH (no fault) before any fault check has run; corrected on the
  first `loop()` iteration once `faultActive()` has an answer.

---

## SAFE

**Entered:** from `INIT` automatically at boot, or from `ILLUMINATING`/`FAULT` as described below.

**Requirements to leave:**
- Button press (PF3, active-LOW, debounced) → **ILLUMINATING**.
- Fault condition detected (active-LOW `FAULT_IN` on PB4 from the upstream board, OR'd with this
  unit's own over-temp via DS18S20 if `HAS_TEMP_SENSOR` is fitted) → **FAULT**.

**Observe:**
- Heartbeat (PE3): single slow pulse — 100 ms on, 900 ms off, repeating. This is the pattern
  currently confirmed working after upload.
- LCD line 1: pulse frequency, e.g. `1000 Hz` (live pot reading). LCD line 2: `P<power> D<duty>%`
  (power to 3 digits — lowest digit of the 10-bit ADC reading dropped), sampled but not yet
  driving any output.
- `ARM_GATE` (PB3): LOW (disarmed) — outputs inhibited.
- `FAULT_OUT` (PB5): HIGH (no fault) — propagated to the downstream board.
- UV active LED (PE0, off-board, active-LOW): off (HIGH).
- TTL pulse output (PB0): disabled, held LOW.
- `Serial2`: `"-> SAFE"` logged on entry (from `ILLUMINATING` as operator stop, or from `FAULT`
  as re-arm).

---

## ILLUMINATING

**Entered:** from `SAFE` by button press, with no fault active.

**Requirements to leave:**
- Fault condition detected → **FAULT** (outputs disabled before the state changes).
- Button press (operator stop) → **SAFE** (outputs disabled before the state changes).

**Observe:**
- Heartbeat (PE3): continuous fast blink — 150 ms on / 150 ms off, no pause. Clearly faster and
  more even than the `SAFE` pulse.
- LCD line 1: pulse frequency (same as `SAFE`). LCD line 2: `P<power> D<duty>%`, now actively
  driving the outputs.
- `ARM_GATE` (PB3): HIGH (armed) — outputs enabled.
- `FAULT_OUT` (PB5): HIGH (no fault) — propagated to the downstream board.
- UV active LED (PE0): on (LOW, active-LOW).
- TTL pulse output (PB0): TCA0 PWM running at the pot-derived frequency/duty; DAC0 output (PD6)
  set from the power pot. DAC0 runs off a fixed 2.048V reference, not VDD — its downstream
  amplifier isn't rail-to-rail on the input side, so DAC0 is kept within a safe input swing and a
  board-side gain stage (R26 = 13.3k, ~2.44x measured) scales that up to the full 0-5V UV
  power-level command range. See `dacInit()` in `src/main.cpp`.
- `Serial2`: `"-> FAULT (from ILLUMINATING)"` or `"-> SAFE (operator stop)"` logged on exit.

---

## FAULT

**Entered:** from `SAFE` or `ILLUMINATING` when a fault condition is detected. Latched.

**Requirements to leave:** both conditions must hold simultaneously —
1. fault condition has cleared, **and**
2. button is pressed (explicit operator re-arm; CN-14/SR-23).

A cleared fault alone does *not* return to `SAFE` — the button press is required so the operator
consciously re-arms the system.

**Observe:**
- Heartbeat (PE3): urgent double-blink — `{100 on, 100 off, 100 on, 600 off}` ms, distinct from
  both `SAFE`'s single pulse and `ILLUMINATING`'s continuous blink.
- LCD line 1: `FAULT`.
- `ARM_GATE` (PB3): LOW — outputs inhibited immediately, ahead of any display/log update.
- `FAULT_OUT` (PB5): LOW — propagated to the downstream board immediately, same ordering
  guarantee as `ARM_GATE`. This is true whether the fault originated locally (own over-temp) or
  came from upstream via `FAULT_IN` — either way it cascades further downstream.
- No dedicated fault LED — `LED_FAULT` (PE1) is confirmed not installed on this application (see
  CLAUDE.md open questions); fault is indicated only via heartbeat pattern and LCD.
- `Serial2`: `"-> FAULT (from SAFE)"` or `"-> FAULT (from ILLUMINATING)"` logged on entry;
  `"-> SAFE (re-armed after fault clear)"` on exit.

---

## State transition summary

```
INIT ──(setup() completes)──> SAFE

SAFE ──(button press)──────────────> ILLUMINATING
SAFE ──(fault detected)────────────> FAULT

ILLUMINATING ──(button press)──────> SAFE
ILLUMINATING ──(fault detected)────> FAULT

FAULT ──(fault cleared AND button press)──> SAFE
```
