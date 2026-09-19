#pragma once

// Copy this file to config.h and fill in real values.
// config.h is gitignored and must not be committed.

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// Optional fallbacks. Leave these placeholders to discover / pair on device.
// The sketch finds the Bridge via mDNS (_hue._tcp), then last-saved IP, then
// this value, then discovery.meethue.com. IP and key are stored in flash (NVS).
#define HUE_BRIDGE_IP "192.168.1.x"
#define HUE_APP_KEY "your-hue-application-key"

// Clip v2 light resource id (UUID). List lights with:
// curl -k -H "hue-application-key: KEY" https://BRIDGE_IP/clip/v2/resource/light
#define HUE_LIGHT_ID "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
