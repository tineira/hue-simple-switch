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

// Improv Serial (https://www.improv-wifi.com/serial) + ASCII HUESET/HUEGET/HUEPAIR/HUECLR.
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
static const unsigned long kImprovByteMs = 500;
static const size_t kUsbImprovMax = 280;
static const size_t kUsbAsciiMax = 192;

inline bool gWifiHaveCreds = false;
inline bool gWifiForgotten = false;

enum UsbParse {
  USB_IDLE = 0,
  USB_IMPROV,
  USB_ASCII,
};

inline UsbParse gUsbParse = USB_IDLE;
inline uint8_t gUsbImprov[kUsbImprovMax];
inline size_t gUsbImprovLen = 0;
inline unsigned long gUsbImprovMs = 0;
inline char gUsbAscii[kUsbAsciiMax];
inline size_t gUsbAsciiLen = 0;

inline bool gImprovConnecting = false;
inline unsigned long gImprovConnectAt = 0;
inline volatile bool gImprovScanPending = false;
inline bool gImprovScanStarted = false;
inline bool gImprovScanDefer = false;
inline unsigned long gImprovScanAt = 0;
inline unsigned long gImprovScanKickAt = 0;

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
  if (gWifiForgotten) {
    gWifiHaveCreds = false;
    return;
  }
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (wifiArduinoCreds()) {
    gWifiHaveCreds = true;
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    WiFi.begin();
    LOG("WiFi: STA ssid=%s\n", (const char *)cfg.sta.ssid);
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

// Borra la STA de Arduino. disconnect(erase) no escribe si no hay asociación.
inline void wifiForgetSta() {
  gWifiForgotten = true;
  gWifiHaveCreds = false;
  gImprovConnecting = false;
  gImprovScanPending = false;
  gImprovScanStarted = false;
  gImprovScanDefer = false;

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  wifi_config_t blank;
  memset(&blank, 0, sizeof(blank));
  esp_wifi_set_config(WIFI_IF_STA, &blank);
  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_disconnect();
    const unsigned long start = millis();
    while (WiFi.status() == WL_CONNECTED && (millis() - start) < 300UL) {
      delay(10);
    }
  }
  memset(&blank, 0, sizeof(blank));
  esp_wifi_set_config(WIFI_IF_STA, &blank);
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
  gImprovScanStarted = false;
  gImprovScanDefer = false;
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
    usbSendState();
    usbSendRpcEmpty(kImprovReqScan);
    return;
  }
  // ACK de estado ya: scanNetworks/mode pueden bloquear el CDC y el wizard
  // ve 4s de silencio. El scan arranca en el siguiente usbPoll.
  gImprovScanPending = true;
  gImprovScanStarted = false;
  gImprovScanDefer = true;
  gImprovScanAt = millis();
  usbSendState();
}

// Lanza el scan async. STA puede estar en WiFi.begin() desde el boot: hay que
// cortar el intento (sin borrar NVS) o scanNetworks falla todo el rato.
inline void usbKickScan() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  gImprovScanKickAt = millis();
}

inline void usbBeginScan() {
  if (!gImprovScanPending || gImprovScanStarted) {
    return;
  }
  gImprovScanStarted = true;
  gImprovScanAt = millis();
  usbKickScan();
}

inline void usbFlushScan() {
  if (!gImprovScanStarted) {
    return;
  }
  const int16_t n = WiFi.scanComplete();
  const unsigned long elapsed = millis() - gImprovScanAt;
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  // FAILED (o 0 muy pronto): reintentar cada ~400 ms hasta 15 s, sin quedarse quieto.
  if ((n == WIFI_SCAN_FAILED || (n == 0 && elapsed < 3000UL)) && elapsed < 15000UL) {
    if (millis() - gImprovScanKickAt >= 400UL) {
      usbKickScan();
    }
    return;
  }
  gImprovScanPending = false;
  gImprovScanStarted = false;
  gImprovScanDefer = false;
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

inline bool usbUnreserved(uint8_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
         c == '_' || c == '~';
}

inline void usbAppendPct(String &out, const char *val) {
  static const char hex[] = "0123456789ABCDEF";
  if (!val) {
    return;
  }
  for (const uint8_t *p = (const uint8_t *)val; *p; p++) {
    const uint8_t c = *p;
    if (usbUnreserved(c)) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
}

inline void usbAppendField(String &out, const char *key, const char *val) {
  out += ' ';
  out += key;
  out += '=';
  usbAppendPct(out, val);
}

inline String usbNvsString(const char *ns, const char *key) {
  Preferences prefs;
  if (!prefs.begin(ns, true)) {
    return String();
  }
  const String v = prefs.getString(key, "");
  prefs.end();
  return v;
}

inline String usbStaSsid() {
  wifi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    return String();
  }
  char ssid[33];
  memset(ssid, 0, sizeof(ssid));
  memcpy(ssid, cfg.sta.ssid, 32);
  memset(&cfg, 0, sizeof(cfg));
  return String(ssid);
}

inline void usbHandleHueget() {
  const bool up = WiFi.status() == WL_CONNECTED;
  const String ssid = usbStaSsid();
  const String ip = up ? WiFi.localIP().toString() : String();
  const String bid = usbNvsString("hue", "bid");
  const String bip = usbNvsString("hue", "ip");
  const String url = usbNvsString("console", "url");
  bool tokenOk = false;
  bool keyOk = false;
  {
    const String token = usbNvsString("console", "token");
    const String key = usbNvsString("hue", "key");
    tokenOk = consoleLooksLikeToken(token);
    keyOk = hueLooksLikeKey(key);
  }

  String line;
  line.reserve(384);
  line = "HUESTA";
  usbAppendField(line, "mac", deviceMacHex().c_str());
  usbAppendField(line, "product", "simple");
  usbAppendField(line, "ver", FIRMWARE_VERSION);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  usbAppendField(line, "chip", "s3");
#else
  usbAppendField(line, "chip", "c6");
#endif
  usbAppendField(line, "ssid", ssid.c_str());
  usbAppendField(line, "wifi", up ? "up" : "down");
  usbAppendField(line, "ip", up ? ip.c_str() : "");
  usbAppendField(line, "bid", bid.c_str());
  usbAppendField(line, "bip", bip.c_str());
  usbAppendField(line, "url", url.c_str());
  usbAppendField(line, "token", tokenOk ? "1" : "0");
  usbAppendField(line, "key", keyOk ? "1" : "0");
  usbReplyLine(line.c_str());
}

inline void usbHandleHuepair() {
  if (WiFi.status() != WL_CONNECTED) {
    usbReplyLine("HUEERR no-wifi");
    return;
  }
  if (!huePairBusy()) {
    huePairSessionBegin();
  }
  usbReplyLine("HUEOK pair");
}

inline void usbHandleHueclr() {
  gNvsEpoch++;
  gNeedConsoleSync = false;
  huePairStop();
  wifiForgetSta();
  consoleForgetSaved();
  hueForgetSaved();
  recipesWipe();
  usbReplyLine("HUEOK clear");
}

inline void usbTrimAscii(char *line) {
  size_t n = strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\r' || line[n - 1] == '\t')) {
    line[--n] = 0;
  }
}

inline void usbHandleAscii(char *line) {
  usbTrimAscii(line);
  if (strcmp(line, "HUEGET") == 0) {
    usbHandleHueget();
    return;
  }
  if (strcmp(line, "HUEPAIR") == 0) {
    usbHandleHuepair();
    return;
  }
  if (strcmp(line, "HUECLR") == 0) {
    usbHandleHueclr();
    return;
  }
  if (strncmp(line, "HUESET", 6) == 0 && (line[6] == 0 || line[6] == ' ')) {
    usbHandleHueset(line);
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
      gUsbImprovMs = millis();
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
    gUsbImprovMs = millis();
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
    usbHandleAscii(gUsbAscii);
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
  if (gUsbParse == USB_IMPROV && gUsbImprovLen > 0 && (millis() - gUsbImprovMs) > kImprovByteMs) {
    gUsbParse = USB_IDLE;
    gUsbImprovLen = 0;
  }
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
    if (gImprovScanDefer) {
      gImprovScanDefer = false;
    } else {
      usbBeginScan();
      usbFlushScan();
    }
  }
  huePairPoll();
}
