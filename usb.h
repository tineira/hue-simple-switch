#pragma once

#include <string.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "log.h"
#include "console.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Improv Serial (https://www.improv-wifi.com/serial) + ASCII HUESET on the same CDC.
// No WebServer / SoftAP. Arduino remembers STA; NVS namespace console holds token/url.

static const uint8_t kImprovVer = 1;
static const uint8_t kImprovState = 0x01;
static const uint8_t kImprovError = 0x02;
static const uint8_t kImprovRpc = 0x03;
static const uint8_t kImprovRpcResult = 0x04;

static const uint8_t kImprovReady = 0x02;
static const uint8_t kImprovProvisioning = 0x03;
static const uint8_t kImprovProvisioned = 0x04;

static const uint8_t kImprovErrNone = 0x00;
static const uint8_t kImprovErrInvalid = 0x01;
static const uint8_t kImprovErrUnknown = 0x02;
static const uint8_t kImprovErrConnect = 0x03;

static const uint8_t kImprovWifiSettings = 0x01;
static const uint8_t kImprovReqState = 0x02;
static const uint8_t kImprovReqInfo = 0x03;
static const uint8_t kImprovReqScan = 0x04;

static const unsigned long kImprovConnectMs = 20000UL;
static const size_t kUsbImprovMax = 280;
static const size_t kUsbAsciiMax = 192;

inline bool gWifiHaveCreds = false;

enum UsbParse {
  USB_IDLE = 0,
  USB_IMPROV,
  USB_ASCII,
};

inline UsbParse gUsbParse = USB_IDLE;
inline uint8_t gUsbImprov[kUsbImprovMax];
inline size_t gUsbImprovLen = 0;
inline char gUsbAscii[kUsbAsciiMax];
inline size_t gUsbAsciiLen = 0;

inline bool gImprovConnecting = false;
inline unsigned long gImprovConnectAt = 0;
inline bool gImprovScanPending = false;
inline unsigned long gImprovScanAt = 0;

inline bool wifiConfigSsidOk() {
  const char *s = WIFI_SSID;
  return s && s[0] && strncmp(s, "your-", 5) != 0;
}

inline bool wifiArduinoCreds() {
  wifi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    return false;
  }
  return cfg.sta.ssid[0] != 0;
}

inline bool usbWifiBusy() {
  return gImprovConnecting || gImprovScanPending;
}

inline uint8_t usbImprovState() {
  if (gImprovConnecting) {
    return kImprovProvisioning;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return kImprovProvisioned;
  }
  return kImprovReady;
}

inline void usbSendRaw(const uint8_t *data, size_t n) {
  Serial.write(data, n);
}

inline void usbSendPacket(uint8_t type, const uint8_t *data, uint8_t len) {
  uint8_t pkt[9 + 255 + 2];
  pkt[0] = 'I';
  pkt[1] = 'M';
  pkt[2] = 'P';
  pkt[3] = 'R';
  pkt[4] = 'O';
  pkt[5] = 'V';
  pkt[6] = kImprovVer;
  pkt[7] = type;
  pkt[8] = len;
  if (len && data) {
    memcpy(pkt + 9, data, len);
  }
  uint16_t sum = 0;
  const size_t body = 9u + len;
  for (size_t i = 0; i < body; i++) {
    sum = (uint16_t)(sum + pkt[i]);
  }
  pkt[body] = (uint8_t)(sum & 0xFF);
  pkt[body + 1] = '\n';
  usbSendRaw(pkt, body + 2);
}

inline void usbSendState() {
  const uint8_t st = usbImprovState();
  usbSendPacket(kImprovState, &st, 1);
}

inline void usbSendError(uint8_t err) {
  usbSendPacket(kImprovError, &err, 1);
}

inline void usbSendRpcResult(uint8_t cmd, const char *const *strs, int n) {
  uint8_t buf[255];
  size_t i = 0;
  buf[i++] = cmd;
  buf[i++] = 0;
  const size_t payloadAt = i;
  for (int s = 0; s < n; s++) {
    const char *str = strs[s] ? strs[s] : "";
    size_t len = strlen(str);
    if (len > 255) {
      len = 255;
    }
    if (i + 1 + len > sizeof(buf)) {
      break;
    }
    buf[i++] = (uint8_t)len;
    memcpy(buf + i, str, len);
    i += len;
  }
  buf[1] = (uint8_t)(i - payloadAt);
  usbSendPacket(kImprovRpcResult, buf, (uint8_t)i);
}

inline void usbSendRpcEmpty(uint8_t cmd) {
  usbSendRpcResult(cmd, nullptr, 0);
}

inline void usbSendNextUrl(uint8_t cmd) {
  const char *empty = "";
  usbSendRpcResult(cmd, &empty, 1);
}

inline void usbReplyLine(const char *s) {
  Serial.print(s);
  Serial.print('\n');
}

inline void wifiBootConnect() {
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (wifiArduinoCreds()) {
    gWifiHaveCreds = true;
    WiFi.begin();
    LOG("WiFi: Arduino STA\n");
    return;
  }
  if (wifiConfigSsidOk()) {
    gWifiHaveCreds = true;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    LOG("WiFi: config.h SSID\n");
    return;
  }
  gWifiHaveCreds = false;
  LOGLN("WiFi: no creds — Improv/HUESET on USB");
}

inline void wifiRetryStored() {
  WiFi.disconnect();
  WiFi.begin();
}

inline void usbHandleWifiSettings(const uint8_t *data, uint8_t dataLen) {
  if (dataLen < 2) {
    usbSendError(kImprovErrInvalid);
    return;
  }
  const uint8_t ssidLen = data[0];
  if ((uint16_t)ssidLen + 1u + 1u > dataLen) {
    usbSendError(kImprovErrInvalid);
    return;
  }
  const uint8_t passLen = data[1 + ssidLen];
  if ((uint16_t)ssidLen + 1u + 1u + passLen != dataLen) {
    usbSendError(kImprovErrInvalid);
    return;
  }
  if (ssidLen == 0 || ssidLen > 32 || passLen > 64) {
    usbSendError(kImprovErrInvalid);
    return;
  }

  char ssid[33];
  char pass[65];
  memcpy(ssid, data + 1, ssidLen);
  ssid[ssidLen] = 0;
  memcpy(pass, data + 2 + ssidLen, passLen);
  pass[passLen] = 0;

  gImprovScanPending = false;
  WiFi.scanDelete();
  gImprovConnecting = true;
  gImprovConnectAt = millis();
  gWifiHaveCreds = true;
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
  usbSendState();
}

inline void usbHandleInfo() {
  const char *info[4] = {"hue-simple-switch", FIRMWARE_VERSION, "XIAO_ESP32C6/esp32-c6", "Hue simple switch"};
  usbSendRpcResult(kImprovReqInfo, info, 4);
}

inline void usbStartScan() {
  if (gImprovConnecting) {
    usbSendRpcEmpty(kImprovReqScan);
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  gImprovScanPending = true;
  gImprovScanAt = millis();
  usbSendState();
}

inline void usbFlushScan() {
  const int16_t n = WiFi.scanComplete();
  const unsigned long elapsed = millis() - gImprovScanAt;
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  if (n == WIFI_SCAN_FAILED && elapsed < 15000UL) {
    return;
  }
  if (n == 0 && elapsed < 3000UL) {
    return;
  }
  gImprovScanPending = false;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      const String ssid = WiFi.SSID(i);
      if (!ssid.length()) {
        continue;
      }
      char rssi[8];
      snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI(i));
      const char *auth = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "NO" : "YES";
      const char *row[3] = {ssid.c_str(), rssi, auth};
      usbSendRpcResult(kImprovReqScan, row, 3);
    }
  }
  usbSendRpcEmpty(kImprovReqScan);
  WiFi.scanDelete();
}

inline void usbHandleRpc(const uint8_t *data, uint8_t len) {
  if (len < 2) {
    usbSendError(kImprovErrInvalid);
    return;
  }
  const uint8_t cmd = data[0];
  const uint8_t dataLen = data[1];
  if ((uint16_t)dataLen + 2u != len) {
    usbSendError(kImprovErrInvalid);
    return;
  }
  usbSendError(kImprovErrNone);
  const uint8_t *payload = data + 2;
  switch (cmd) {
    case kImprovWifiSettings:
      usbHandleWifiSettings(payload, dataLen);
      break;
    case kImprovReqState:
      usbSendState();
      if (usbImprovState() == kImprovProvisioned) {
        usbSendNextUrl(kImprovReqState);
      }
      break;
    case kImprovReqInfo:
      usbHandleInfo();
      break;
    case kImprovReqScan:
      usbStartScan();
      break;
    default:
      usbSendError(kImprovErrUnknown);
      break;
  }
}

inline void usbHandleImprovPacket() {
  if (gUsbImprovLen < 10) {
    return;
  }
  if (memcmp(gUsbImprov, "IMPROV", 6) != 0) {
    return;
  }
  const uint8_t ver = gUsbImprov[6];
  const uint8_t type = gUsbImprov[7];
  const uint8_t dlen = gUsbImprov[8];
  if (ver != kImprovVer) {
    return;
  }
  if (gUsbImprovLen != (size_t)9 + dlen + 1) {
    return;
  }
  uint16_t sum = 0;
  for (size_t i = 0; i + 1 < gUsbImprovLen; i++) {
    sum = (uint16_t)(sum + gUsbImprov[i]);
  }
  if ((uint8_t)(sum & 0xFF) != gUsbImprov[gUsbImprovLen - 1]) {
    usbSendError(kImprovErrInvalid);
    return;
  }
  if (type != kImprovRpc) {
    return;
  }
  usbHandleRpc(gUsbImprov + 9, dlen);
}

inline void usbHandleHueset(char *line) {
  // line: "HUESET token …" / "HUESET url …"
  if (strncmp(line, "HUESET", 6) != 0) {
    return;
  }
  char *p = line + 6;
  while (*p == ' ') {
    p++;
  }
  if (!*p) {
    usbReplyLine("HUEERR syntax");
    return;
  }
  char *key = p;
  while (*p && *p != ' ') {
    p++;
  }
  char *val = nullptr;
  if (*p == ' ') {
    *p++ = 0;
    while (*p == ' ') {
      p++;
    }
    val = p;
  } else {
    val = p;
  }
  if (!val) {
    val = (char *)"";
  }
  size_t n = strlen(val);
  while (n && (val[n - 1] == ' ' || val[n - 1] == '\r')) {
    val[--n] = 0;
  }

  if (strcmp(key, "token") == 0) {
    if (!n) {
      usbReplyLine("HUEERR empty token");
      return;
    }
    if (!consoleSetToken(val)) {
      usbReplyLine("HUEERR token");
      return;
    }
    gNeedConsoleSync = true;
    usbReplyLine("HUEOK token");
    return;
  }
  if (strcmp(key, "url") == 0) {
    if (!n) {
      usbReplyLine("HUEERR empty url");
      return;
    }
    if (strncmp(val, "https://", 8) != 0 && strncmp(val, "http://", 7) != 0) {
      usbReplyLine("HUEERR url");
      return;
    }
    if (!consoleSetUrl(val)) {
      usbReplyLine("HUEERR url");
      return;
    }
    gNeedConsoleSync = true;
    usbReplyLine("HUEOK url");
    return;
  }
  usbReplyLine("HUEERR unknown");
}

inline void usbFeed(uint8_t b) {
  if (gUsbParse == USB_IDLE) {
    if (b == '\n' || b == '\r') {
      return;
    }
    if (b == 'I') {
      gUsbParse = USB_IMPROV;
      gUsbImprovLen = 0;
      gUsbImprov[gUsbImprovLen++] = b;
      return;
    }
    if (b >= 32 && b < 127) {
      gUsbParse = USB_ASCII;
      gUsbAsciiLen = 0;
      gUsbAscii[gUsbAsciiLen++] = (char)b;
    }
    return;
  }

  if (gUsbParse == USB_IMPROV) {
    if (gUsbImprovLen >= kUsbImprovMax) {
      gUsbParse = USB_IDLE;
      gUsbImprovLen = 0;
      return;
    }
    gUsbImprov[gUsbImprovLen++] = b;
    if (gUsbImprovLen == 6 && memcmp(gUsbImprov, "IMPROV", 6) != 0) {
      gUsbParse = USB_IDLE;
      gUsbImprovLen = 0;
      return;
    }
    if (gUsbImprovLen >= 9) {
      const uint8_t dlen = gUsbImprov[8];
      const size_t need = (size_t)9 + dlen + 1;
      if (need > kUsbImprovMax) {
        gUsbParse = USB_IDLE;
        gUsbImprovLen = 0;
        return;
      }
      if (gUsbImprovLen == need) {
        usbHandleImprovPacket();
        gUsbParse = USB_IDLE;
        gUsbImprovLen = 0;
      }
    }
    return;
  }

  // ASCII
  if (b == '\n') {
    if (gUsbAsciiLen && gUsbAscii[gUsbAsciiLen - 1] == '\r') {
      gUsbAsciiLen--;
    }
    gUsbAscii[gUsbAsciiLen] = 0;
    usbHandleHueset(gUsbAscii);
    gUsbParse = USB_IDLE;
    gUsbAsciiLen = 0;
    return;
  }
  if (gUsbAsciiLen + 1 >= kUsbAsciiMax) {
    gUsbParse = USB_IDLE;
    gUsbAsciiLen = 0;
    return;
  }
  gUsbAscii[gUsbAsciiLen++] = (char)b;
}

inline void usbPoll() {
  while (Serial.available() > 0) {
    usbFeed((uint8_t)Serial.read());
  }

  if (gImprovConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      gImprovConnecting = false;
      gWifiHaveCreds = true;
      usbSendState();
      usbSendNextUrl(kImprovWifiSettings);
    } else if (millis() - gImprovConnectAt >= kImprovConnectMs) {
      gImprovConnecting = false;
      usbSendError(kImprovErrConnect);
      usbSendState();
    }
  }

  if (gImprovScanPending) {
    usbFlushScan();
  }
}
