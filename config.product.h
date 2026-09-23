#pragma once

// config.h for the web-installer binary (hue.tineira.com): no USB logs.
// Copy to config.h before: arduino-cli compile --profile xiao-c6 --export-binaries .
// Wi-Fi is Arduino STA (Improv). Token/url live in NVS namespace console (HUESET).

#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif
