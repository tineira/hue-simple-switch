#pragma once

// Copy this file to config.h. config.h is gitignored.
//
// Wi-Fi, console URL and console token are not compiled in. The console at
// hue.tineira.com writes them over USB (Improv for Wi-Fi, HUESET for token/url)
// and they survive later uploads. Bridge IP, Hue application key and recipes
// come from mDNS / pair / NVS / poll.

// 1 = USB serial logs (Serial Monitor). 0 = CDC on, no logs (Improv / HUESET).
#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 0
#endif
