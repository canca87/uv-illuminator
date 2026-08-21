// Diagnostic logging over DEBUG_SERIAL (see pins.h), gated behind ENABLE_DEBUG_LOG so production
// builds don't pay the runtime cost (formatting + blocking USART writes) of a log call on every
// loop() iteration. Not a peripheral driver, so this lives outside pins.h (constants only).
//
// Build-flag controlled: default (ENABLE_DEBUG_LOG=0) compiles every LOG_* call away entirely.
// Use the curiosity_nano_db_debug PlatformIO env (ENABLE_DEBUG_LOG=1) for development.
#pragma once

#include "pins.h"

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 0
#endif

#if ENABLE_DEBUG_LOG
#define LOG_BEGIN(...)   DEBUG_SERIAL.begin(__VA_ARGS__)
#define LOG_PRINT(...)   DEBUG_SERIAL.print(__VA_ARGS__)
#define LOG_PRINTLN(...) DEBUG_SERIAL.println(__VA_ARGS__)
#else
#define LOG_BEGIN(...)
#define LOG_PRINT(...)
#define LOG_PRINTLN(...)
#endif
