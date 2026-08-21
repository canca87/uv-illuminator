// Pin mapping for the UV-illuminator application, laser-controller rev_1 PCB (AVR128DB48).
// Source of truth: ../pins.md — read that file before changing anything here.
// Do not use the board package's default `Serial` (-> Serial3 -> PB0/PB1) or `LED_BUILTIN`
// (-> PB3) macros on this PCB: those pins are repurposed (see pins.md gotchas section).
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Temperature sensor population — whether this physical unit has a DS18S20 fitted (PF2).
// Independent of the fault in/out pins below, which are wired and active on every unit
// regardless. See pins.md "Fault chain" / CLAUDE.md "Fault chain (multi-board fault topology)".
// ---------------------------------------------------------------------------
#ifndef HAS_TEMP_SENSOR
#define HAS_TEMP_SENSOR 1
#endif

// ---------------------------------------------------------------------------
// UV source output control
// ---------------------------------------------------------------------------
#define PIN_TTL_PULSE_OUT   PIN_PB0   // TCA0 WO0, on/off pulse control
#define PIN_DAC_OUT         PIN_PD6   // DAC0, fixed silicon pin, UV power level

// ---------------------------------------------------------------------------
// ARM gate — safety output enable. Drive HIGH to arm, LOW/Hi-Z to inhibit.
// ---------------------------------------------------------------------------
#define PIN_ARM_GATE        PIN_PB3

// ---------------------------------------------------------------------------
// LCD — 16x2 character, 4-bit parallel (LiquidCrystal library pin order: RS, E, DB4-DB7)
// ---------------------------------------------------------------------------
#define PIN_LCD_RS          PIN_PA2
#define PIN_LCD_E           PIN_PA3
#define PIN_LCD_DB4         PIN_PA4
#define PIN_LCD_DB5         PIN_PA5
#define PIN_LCD_DB6         PIN_PA6
#define PIN_LCD_DB7         PIN_PA7

// ---------------------------------------------------------------------------
// Manual controls
// ---------------------------------------------------------------------------
#define PIN_POT_PWR         PIN_PD0   // ADC — UV power-level setpoint
#define PIN_POT_DC          PIN_PD1   // ADC — duty cycle setpoint
#define PIN_POT_FREQ        PIN_PD2   // ADC — pulse frequency setpoint
#define PIN_BUTTON_ON_OFF   PIN_PF3   // Momentary, active-LOW, PCB pull-up (J21)

// ---------------------------------------------------------------------------
// Temperature sensor — only present if HAS_TEMP_SENSOR (see above)
// ---------------------------------------------------------------------------
#if HAS_TEMP_SENSOR
#define PIN_ONE_WIRE        PIN_PF2   // DS18S20, pull-up on PCB
#endif

// ---------------------------------------------------------------------------
// Fault chain — every unit both consumes an upstream fault signal and produces one downstream.
// Combined with the over-temp check (if HAS_TEMP_SENSOR) via OR: either condition drives
// PIN_FAULT_OUT low and takes this unit to FAULT. See pins.md "Fault chain".
// ---------------------------------------------------------------------------
#define PIN_FAULT_IN         PIN_PB4   // J18, active-LOW TTL fault-in from upstream board's PB5
#define PIN_FAULT_OUT        PIN_PB5   // J19, active-LOW TTL fault-out to downstream board's PB4

// ---------------------------------------------------------------------------
// Feedback LED — off-board panel, active-LOW (J22)
// ---------------------------------------------------------------------------
#define PIN_UV_ACTIVE_LED   PIN_PE0

// ---------------------------------------------------------------------------
// On-board heartbeat LED — state-dependent blink patterns, see main.cpp PATTERN_* tables
// ---------------------------------------------------------------------------
#define PIN_LED_HEARTBEAT   PIN_PE3

// ---------------------------------------------------------------------------
// Debug UART — USART2 (`Serial2`), diagnostic log output only, not a CLI
// ---------------------------------------------------------------------------
#define DEBUG_SERIAL        Serial2
#define DEBUG_BAUD          115200
