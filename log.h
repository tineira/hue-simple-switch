#pragma once

// SERIAL_DEBUG en config.h (0 = binario de producto: CDC abierto, sin logs USB).
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif

#if SERIAL_DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#define LOGS(s) Serial.print(s)
#define LOGLN(s) Serial.println(s)
#else
#define LOG(...) ((void)0)
#define LOGS(s) ((void)0)
#define LOGLN(s) ((void)0)
#endif
