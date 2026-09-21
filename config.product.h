#pragma once

// Empty defines for the web-installer binary (hue.tineira.com).
// Copy to config.h before: arduino-cli compile --profile xiao-c6 --export-binaries .
// Wi-Fi is Arduino STA (Improv). Token/url live in NVS namespace console.

#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define CONSOLE_URL ""
#define CONSOLE_TOKEN ""

#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif
