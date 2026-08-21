// UV-illuminator application firmware.
// Architecture: linear superloop, single top-level state machine, non-blocking millis()-based
// periodic tasks. No RTOS — see ../CLAUDE.md "Architecture" for rationale.
#include <Arduino.h>
#include <LiquidCrystal.h>
#include "pins.h"
#include "debug_log.h"

#if HAS_TEMP_SENSOR
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class State { INIT, SAFE, ILLUMINATING, FAULT };
static State state = State::INIT;

// ---------------------------------------------------------------------------
// Heartbeat LED (PE3) — state-dependent blink patterns, so the operating state is readable
// at a glance without the LCD. Each pattern is a repeating sequence of on/off steps; all step
// durations are >=100 ms so every transition is clearly visible to the eye.
// ---------------------------------------------------------------------------
struct BlinkStep { uint16_t durationMs; bool ledOn; };

static const BlinkStep PATTERN_INIT[] = {
  {100, true}, {100, false}, {100, true}, {100, false}, {100, true}, {500, false},
};  // fast triple-blink — starting up

static const BlinkStep PATTERN_SAFE[] = {
  {100, true}, {900, false},
};  // single slow pulse — idle, disarmed

static const BlinkStep PATTERN_ILLUMINATING[] = {
  {150, true}, {150, false},
};  // continuous fast blink — armed and active

static const BlinkStep PATTERN_FAULT[] = {
  {100, true}, {100, false}, {100, true}, {600, false},
};  // urgent double-blink — clearly distinct from the continuous ILLUMINATING blink

// ---------------------------------------------------------------------------
// Peripherals
// ---------------------------------------------------------------------------
static LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_E, PIN_LCD_DB4, PIN_LCD_DB5, PIN_LCD_DB6, PIN_LCD_DB7);

#if HAS_TEMP_SENSOR
static OneWire oneWire(PIN_ONE_WIRE);
static DallasTemperature tempSensor(&oneWire);
static const float OVER_TEMP_LIMIT_C = 60.0f;  // TODO: confirm application over-temp threshold
static float lastTempC = 0.0f;  // cached between 1 s conversions, shared with logIoSnapshot()
#endif

// ---------------------------------------------------------------------------
// Setpoints, sampled from the three pots
// ---------------------------------------------------------------------------
static uint16_t setpointPower = 0;      // raw ADC, 0-1023, drives DAC_OUT
static uint16_t setpointDutyPct = 0;    // 0-100
static uint32_t setpointFreqHz = 1000;  // TODO: confirm application frequency range/mapping

// ---------------------------------------------------------------------------
// Button (active-LOW, debounced)
// ---------------------------------------------------------------------------
static bool buttonPressedEvent();

// ---------------------------------------------------------------------------
// Fault check — active-LOW upstream FAULT_IN (PB4, every unit) OR'd with this unit's own
// over-temp check (if HAS_TEMP_SENSOR).
// ---------------------------------------------------------------------------
static bool faultActive();

// ---------------------------------------------------------------------------
// TCA0 single-slope PWM on WO0 (PB0) — direct register access per project convention
// ---------------------------------------------------------------------------
static void pulseInit();
static void pulseSetFrequency(uint32_t freqHz);
static void pulseSetDutyPercent(uint8_t percent);
static void pulseEnable(bool on);

// ---------------------------------------------------------------------------
// DAC0 (PD6) — direct register access, full 10-bit range
// ---------------------------------------------------------------------------
static void dacInit();

// ---------------------------------------------------------------------------
// ARM gate
// ---------------------------------------------------------------------------
static inline void armGate(bool armed) {
  digitalWrite(PIN_ARM_GATE, armed ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Fault-out (PB5, J19) — active-LOW TTL fault signal to the next board's FAULT_IN (PB4). Driven
// every loop() iteration, immediately after faultActive() and ahead of any state/display/log
// update, same ordering guarantee as ARM_GATE.
// ---------------------------------------------------------------------------
static inline void faultOut(bool fault) {
  digitalWrite(PIN_FAULT_OUT, fault ? LOW : HIGH);
}

// ---------------------------------------------------------------------------
// Periodic task scheduling
// ---------------------------------------------------------------------------
static void samplePots();
static void updateOutputs();
static void updateLcd();
static void updateHeartbeat();
static void updateUvActiveLed();
static void logIoSnapshot(bool fault);

void setup() {
  LOG_BEGIN(DEBUG_BAUD);
  LOG_PRINTLN(F("UV-illuminator firmware starting"));
#if HAS_TEMP_SENSOR
  LOG_PRINTLN(F("temp sensor: DS18S20 on PF2, fitted"));
#else
  LOG_PRINTLN(F("temp sensor: not fitted on this unit"));
#endif

  pinMode(PIN_ARM_GATE, OUTPUT);
  armGate(false);  // safe state first, before anything else
  LOG_PRINTLN(F("[init] ARM_GATE (PB3) -> LOW"));

  pinMode(PIN_BUTTON_ON_OFF, INPUT_PULLUP);
  LOG_PRINTLN(F("[init] button (PF3) INPUT_PULLUP"));

  pinMode(PIN_UV_ACTIVE_LED, OUTPUT);
  digitalWrite(PIN_UV_ACTIVE_LED, HIGH);  // active-LOW: HIGH = off
  LOG_PRINTLN(F("[init] UV_ACTIVE_LED (PE0) -> HIGH (off)"));

  pinMode(PIN_LED_HEARTBEAT, OUTPUT);
  LOG_PRINTLN(F("[init] LED_HEARTBEAT (PE3) OUTPUT"));

  pinMode(PIN_FAULT_IN, INPUT_PULLUP);
  LOG_PRINTLN(F("[init] FAULT_IN (PB4) INPUT_PULLUP"));

  pinMode(PIN_FAULT_OUT, OUTPUT);
  faultOut(false);  // no fault asserted yet — corrected on the first loop() as soon as it's known
  LOG_PRINTLN(F("[init] FAULT_OUT (PB5) -> HIGH (no fault)"));

#if HAS_TEMP_SENSOR
  tempSensor.begin();
  LOG_PRINT(F("[init] DS18S20 (PF2) devices found: "));
  LOG_PRINTLN(tempSensor.getDeviceCount());
#endif

  lcd.begin(16, 2);
  lcd.print(F("UV Illuminator"));
  LOG_PRINTLN(F("[init] LCD (PA2-PA7) begin()/print() issued"));

  pulseInit();
  LOG_PRINTLN(F("[init] pulse timer (TCA0/PB0) ready"));

  dacInit();
  LOG_PRINTLN(F("[init] DAC0 (PD6) ready, VREF=2.048V"));

  state = State::SAFE;
  LOG_PRINTLN(F("-> SAFE"));
}

void loop() {
  samplePots();
  updateHeartbeat();

  bool fault = faultActive();
  faultOut(fault);  // propagate to the downstream board immediately, ahead of state/display/log
  bool pressed = buttonPressedEvent();

  switch (state) {
    case State::INIT:
      // Never reached after setup() completes.
      break;

    case State::SAFE:
      armGate(false);
      pulseEnable(false);
      if (fault) {
        state = State::FAULT;
        LOG_PRINTLN(F("-> FAULT (from SAFE)"));
      } else if (pressed) {
        state = State::ILLUMINATING;
        LOG_PRINTLN(F("-> ILLUMINATING"));
      }
      break;

    case State::ILLUMINATING:
      if (fault) {
        armGate(false);
        pulseEnable(false);
        state = State::FAULT;
        LOG_PRINTLN(F("-> FAULT (from ILLUMINATING)"));
      } else if (pressed) {
        armGate(false);
        pulseEnable(false);
        state = State::SAFE;
        LOG_PRINTLN(F("-> SAFE (operator stop)"));
      } else {
        armGate(true);
        pulseEnable(true);
        updateOutputs();
      }
      break;

    case State::FAULT:
      armGate(false);
      pulseEnable(false);
      // Fault LED not yet implemented — see CLAUDE.md open questions.
      // Latched: fault must clear AND operator must press the button to re-arm (CN-14/SR-23).
      if (!fault && pressed) {
        state = State::SAFE;
        LOG_PRINTLN(F("-> SAFE (re-armed after fault clear)"));
      }
      break;
  }

  updateUvActiveLed();
  updateLcd();
  logIoSnapshot(fault);
}

// ---------------------------------------------------------------------------
static void samplePots() {
  static uint32_t lastSample = 0;
  uint32_t now = millis();
  if (now - lastSample < 20) return;
  lastSample = now;

  setpointPower = analogRead(PIN_POT_PWR);
  setpointDutyPct = map(analogRead(PIN_POT_DC), 0, 1023, 0, 100);
  // TODO: confirm frequency mapping range for this application.
  setpointFreqHz = map(analogRead(PIN_POT_FREQ), 0, 1023, 1, 20000);
}

static void updateOutputs() {
  DAC0.DATA = setpointPower << 6;  // 10-bit setpoint, left-justified per DAC0.DATA layout
  pulseSetFrequency(setpointFreqHz);
  pulseSetDutyPercent((uint8_t)setpointDutyPct);
}

static void updateUvActiveLed() {
  digitalWrite(PIN_UV_ACTIVE_LED, state == State::ILLUMINATING ? HIGH : LOW);  // active-LOW
}

static void heartbeatPattern(State s, const BlinkStep** pattern, uint8_t* len) {
  switch (s) {
    case State::INIT:
      *pattern = PATTERN_INIT;
      *len = sizeof(PATTERN_INIT) / sizeof(PATTERN_INIT[0]);
      break;
    case State::SAFE:
      *pattern = PATTERN_SAFE;
      *len = sizeof(PATTERN_SAFE) / sizeof(PATTERN_SAFE[0]);
      break;
    case State::ILLUMINATING:
      *pattern = PATTERN_ILLUMINATING;
      *len = sizeof(PATTERN_ILLUMINATING) / sizeof(PATTERN_ILLUMINATING[0]);
      break;
    case State::FAULT:
      *pattern = PATTERN_FAULT;
      *len = sizeof(PATTERN_FAULT) / sizeof(PATTERN_FAULT[0]);
      break;
  }
}

static void updateHeartbeat() {
  static State lastState = State::INIT;
  static uint8_t stepIndex = 0;
  static uint32_t stepStart = 0;

  const BlinkStep* pattern;
  uint8_t len;
  heartbeatPattern(state, &pattern, &len);

  if (state != lastState) {
    lastState = state;
    stepIndex = 0;
    stepStart = millis();
    digitalWrite(PIN_LED_HEARTBEAT, pattern[0].ledOn ? HIGH : LOW);
    return;
  }

  if (millis() - stepStart >= pattern[stepIndex].durationMs) {
    stepStart = millis();
    stepIndex = (stepIndex + 1) % len;
    digitalWrite(PIN_LED_HEARTBEAT, pattern[stepIndex].ledOn ? HIGH : LOW);
  }
}

static void updateLcd() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 200) return;
  lastUpdate = now;

  lcd.setCursor(0, 0);
  if (state == State::FAULT) {
    lcd.print(F("FAULT           "));
  } else {
    // Frequency takes the whole top row instead of a state name — width varies with the
    // pot-driven value, so pad to 16 cols to clear whatever was there before (e.g. "FAULT").
    char freq[13];
    snprintf(freq, sizeof(freq), "%lu Hz", (unsigned long)setpointFreqHz);
    char line0[17];
    snprintf(line0, sizeof(line0), "%-16s", freq);
    lcd.print(line0);
  }

  lcd.setCursor(0, 1);
  char line1[17];
  // Power shown to 3 digits (drop the lowest digit of the 10-bit 0-1023 ADC reading).
  snprintf(line1, sizeof(line1), "P%3d D%3d%%", setpointPower / 10, setpointDutyPct);
  lcd.print(line1);
}

static bool buttonPressedEvent() {
  static bool lastRaw = true;   // idle-HIGH (active-LOW, pull-up)
  static bool debounced = true;
  static uint32_t lastChange = 0;

  bool raw = digitalRead(PIN_BUTTON_ON_OFF);
  uint32_t now = millis();
  if (raw != lastRaw) {
    lastChange = now;
    lastRaw = raw;
    LOG_PRINT(F("[button] raw edge -> "));
    LOG_PRINTLN(raw == LOW ? F("LOW") : F("HIGH"));
  }
  bool eventFired = false;
  if ((now - lastChange) > 30 && raw != debounced) {
    debounced = raw;
    if (debounced == LOW) {
      eventFired = true;  // falling edge = press
      LOG_PRINTLN(F("[button] debounced press event"));
    }
  }
  return eventFired;
}

static bool faultActive() {
  static bool first = true;
  static bool lastRaw = false;
  bool faultIn = digitalRead(PIN_FAULT_IN) == LOW;  // active-LOW
  if (first || faultIn != lastRaw) {
    first = false;
    lastRaw = faultIn;
    LOG_PRINT(F("[fault_in] raw edge -> "));
    LOG_PRINTLN(faultIn ? F("LOW (fault)") : F("HIGH (ok)"));
  }

#if HAS_TEMP_SENSOR
  static uint32_t lastRead = 0;
  uint32_t now = millis();
  if (now - lastRead >= 1000) {
    lastRead = now;
    tempSensor.requestTemperatures();
    lastTempC = tempSensor.getTempCByIndex(0);
    LOG_PRINT(F("[temp] "));
    LOG_PRINT(lastTempC);
    LOG_PRINTLN(F("C"));
  }
  bool overTemp = lastTempC >= OVER_TEMP_LIMIT_C;
  return faultIn || overTemp;
#else
  return faultIn;
#endif
}

// ---------------------------------------------------------------------------
// Periodic aggregate IO snapshot — one line covering every subsystem, so each part can be
// wired in individually and confirmed against this log before moving to the next.
// ---------------------------------------------------------------------------
static void logIoSnapshot(bool fault) {
  static uint32_t lastLog = 0;
  uint32_t now = millis();
  if (now - lastLog < 500) return;
  lastLog = now;

  const __FlashStringHelper* stateName;
  switch (state) {
    case State::INIT:         stateName = F("INIT"); break;
    case State::SAFE:         stateName = F("SAFE"); break;
    case State::ILLUMINATING: stateName = F("ILLUMINATING"); break;
    case State::FAULT:        stateName = F("FAULT"); break;
  }

  LOG_PRINT(F("[IO] state="));
  LOG_PRINT(stateName);
  LOG_PRINT(F(" pwr="));
  LOG_PRINT(setpointPower);
  LOG_PRINT(F(" dc="));
  LOG_PRINT(setpointDutyPct);
  LOG_PRINT(F("% freq="));
  LOG_PRINT(setpointFreqHz);
  LOG_PRINT(F("Hz button="));
  LOG_PRINT(digitalRead(PIN_BUTTON_ON_OFF) == LOW ? F("LOW") : F("HIGH"));
  LOG_PRINT(F(" fault="));
  LOG_PRINT(fault ? F("1") : F("0"));
  LOG_PRINT(F(" fault_in="));
  LOG_PRINT(digitalRead(PIN_FAULT_IN) == LOW ? F("LOW") : F("HIGH"));
  LOG_PRINT(F(" fault_out="));
  LOG_PRINT(digitalRead(PIN_FAULT_OUT) == LOW ? F("LOW") : F("HIGH"));
#if HAS_TEMP_SENSOR
  LOG_PRINT(F(" temp="));
  LOG_PRINT(lastTempC);
  LOG_PRINT(F("C"));
#endif
  LOG_PRINT(F(" ARM_GATE="));
  LOG_PRINT(digitalRead(PIN_ARM_GATE) == HIGH ? F("HIGH") : F("LOW"));
  LOG_PRINT(F(" UV_LED="));
  LOG_PRINT(digitalRead(PIN_UV_ACTIVE_LED) == LOW ? F("ON") : F("OFF"));
  LOG_PRINT(F(" heartbeat="));
  LOG_PRINT(digitalRead(PIN_LED_HEARTBEAT) == HIGH ? F("ON") : F("OFF"));
  LOG_PRINT(F(" pulse_en="));
  LOG_PRINT((TCA0.SINGLE.CTRLA & TCA_SINGLE_ENABLE_bm) ? F("1") : F("0"));
  LOG_PRINT(F(" dac="));
  LOG_PRINTLN(setpointPower);  // 10-bit code loaded into DAC0.DATA (<<6), 0-1023
}

// ---------------------------------------------------------------------------
// TCA0 single-slope PWM, WO0 only (PB0). Direct register access — see CLAUDE.md.
// TODO: verify prescaler/PER selection against real hardware across the full app freq range.
// ---------------------------------------------------------------------------
static void pulseInit() {
  pinMode(PIN_TTL_PULSE_OUT, OUTPUT);

  // DxCore's init_TCA0() (runs automatically before setup(), for its own analogWrite() PWM
  // support) leaves TCA0 running in SPLIT mode. takeOverTCA0() stops it, hard-resets it via
  // CTRLESET (which clears SPLITM back to single mode along with everything else), and marks
  // TCA0 as user-controlled so the core's analogWrite()/digitalWrite() machinery leaves it
  // alone. Required before any direct single-slope register access below.
  takeOverTCA0();

  // DxCore's 48pin-standard variant (this board) defaults PORTMUX.TCAROUTEA to route TCA0 to
  // PORTC (see variant pins_arduino.h: `#define TCA0_PINS PORTMUX_TCA0_PORTC_gc`), not PORTB.
  // WO0 must be explicitly routed to PORTB to reach PB0 (TTL_PULSE_OUT).
  PORTMUX.TCAROUTEA = (PORTMUX.TCAROUTEA & ~PORTMUX_TCA0_gm) | PORTMUX_TCA0_PORTB_gc;
  TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV64_gc;  // re-selected per-frequency in pulseSetFrequency()
  pulseSetFrequency(setpointFreqHz);
  pulseSetDutyPercent(0);
}

static void pulseSetFrequency(uint32_t freqHz) {
  if (freqHz < 1) freqHz = 1;

  static const struct { uint16_t div; uint8_t gc; } prescalers[] = {
    {1, TCA_SINGLE_CLKSEL_DIV1_gc},   {2, TCA_SINGLE_CLKSEL_DIV2_gc},
    {4, TCA_SINGLE_CLKSEL_DIV4_gc},   {8, TCA_SINGLE_CLKSEL_DIV8_gc},
    {16, TCA_SINGLE_CLKSEL_DIV16_gc}, {64, TCA_SINGLE_CLKSEL_DIV64_gc},
    {256, TCA_SINGLE_CLKSEL_DIV256_gc}, {1024, TCA_SINGLE_CLKSEL_DIV1024_gc},
  };

  for (auto &p : prescalers) {
    uint32_t per = (F_CPU / p.div / freqHz);
    if (per >= 1 && per <= 65536) {
      TCA0.SINGLE.CTRLA = (TCA0.SINGLE.CTRLA & ~TCA_SINGLE_CLKSEL_gm) | p.gc;
      TCA0.SINGLE.PER = (uint16_t)(per - 1);
      return;
    }
  }
  // Frequency out of achievable range — clamp to slowest available.
  TCA0.SINGLE.CTRLA = (TCA0.SINGLE.CTRLA & ~TCA_SINGLE_CLKSEL_gm) | TCA_SINGLE_CLKSEL_DIV1024_gc;
  TCA0.SINGLE.PER = 0xFFFF;
}

static void pulseSetDutyPercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint32_t per = TCA0.SINGLE.PER;
  TCA0.SINGLE.CMP0BUF = (uint16_t)((per * percent) / 100);
}

static void pulseEnable(bool on) {
  TCA0.SINGLE.CTRLA = on ? (TCA0.SINGLE.CTRLA | TCA_SINGLE_ENABLE_bm)
                         : (TCA0.SINGLE.CTRLA & ~TCA_SINGLE_ENABLE_bm);
  if (on) {
    TCA0.SINGLE.CTRLB |= TCA_SINGLE_CMP0EN_bm;
  } else {
    TCA0.SINGLE.CTRLB &= ~TCA_SINGLE_CMP0EN_bm;
    digitalWrite(PIN_TTL_PULSE_OUT, LOW);
  }
}

// ---------------------------------------------------------------------------
// DAC0 (PD6), full 10-bit range. VREF = 2.048V, not VDD: the downstream amplifier's input isn't
// rail-to-rail, so DAC0's output has to stay within its safe input range rather than swinging
// full-scale to VDD. A board-side gain stage (R26 = 13.3k) then scales that up to the full 0-5V
// UV power-level command range. Measured closed-loop gain is ~2.44x, not the ~1.15x originally
// calculated for that feedback network — confirmed working on hardware; the discrepancy hasn't
// been root-caused and isn't blocking, so it's noted here rather than re-derived.
// ---------------------------------------------------------------------------
static void dacInit() {
  VREF.DAC0REF = (VREF.DAC0REF & ~VREF_REFSEL_gm) | VREF_REFSEL_2V048_gc;
  DAC0.CTRLA = DAC_ENABLE_bm | DAC_OUTEN_bm;
}
