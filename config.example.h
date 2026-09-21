#pragma once

// Copy this file to config.h and fill in real values.
// config.h is gitignored and must not be committed.
// Product installer binary uses config.product.h (empty SSID / token / URL).

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// Device console (not the Hue Bridge). Token is minted in the console UI.
// CONSOLE_URL    public host, e.g. https://hue.tineira.com
//                (local dev: http://localhost:3000 — no TLS)
// CONSOLE_TOKEN  device API key (hsw_…), sent as Authorization: Bearer
// USB install writes the same keys to NVS namespace console (token, url);
// those win over these #defines when set.
// Do not put SSID, passwords, Hue keys, or this token in git.
#define CONSOLE_URL "https://hue.tineira.com"
#define CONSOLE_TOKEN "your-console-token"

// 1 = USB serial logs (Serial Monitor). 0 = CDC on, no logs (Improv / HUESET).
#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif

// Bridge IP, Hue application key, and recipes are not stored here
// (mDNS / pair / NVS / poll).
