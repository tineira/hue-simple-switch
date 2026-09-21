#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif
#include "log.h"
#include <WiFi.h>

String gHueBridgeIp;
String gHueAppKey;

#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"
#include "channels.h"
#include "console.h"
#include "usb.h"

static bool gWifiWasUp = false;
static bool gHueReady = false;
static bool gWifiBootTried = false;
static unsigned long gWifiLastTryMs = 0;

static void usbPump(unsigned long ms) {
  const unsigned long start = millis();
  usbPoll();
  while (millis() - start < ms) {
    delay(10);
    usbPoll();
  }
}

static void afterWifiUp() {
  LOG("IP: ");
  LOGLN(WiFi.localIP());
  LOG("mac %s\n", deviceMacHex().c_str());
  digitalWrite(LED_BUILTIN, HIGH);
  if (!hueEnsureReady()) {
    LOGLN("Hue setup failed — press Bridge button if pairing, check Wi-Fi LAN");
    gHueReady = false;
    return;
  }
  gHueReady = true;
  LOG("Using Bridge %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
  if (recipesBindBridge(gHueBridgeId)) {
    gConsoleRegistered = false;
  }
  if (!consoleConfigured()) {
    LOGLN("console: CONSOLE_URL / CONSOLE_TOKEN not set");
  }
  gNeedConsoleSync = true;
}

void setup() {
  Serial.begin(115200);
  // USB CDC: sin PC el write() espera al host. 100 ms: 0 descartaba escrituras si tx_lock estaba ocupado.
  Serial.setTxTimeoutMs(100);
  usbPoll();
  usbPump(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  LOG("hue-simple-switch\n");
  LOG("firmware %s\n", FIRMWARE_VERSION);

  gOnHueWait = []() { usbPoll(); };

  consoleLoadNvs();
  recipesLoad();
  channelsBegin();
  consoleWorkerBegin();
  usbPoll();

  // Web Serial DTR-resets the C6; Scan/ping can arrive during boot.
  // Do not WiFi.begin over an Improv scan, and keep usbPoll alive if STA fails.
  usbPump(1000);
  if (!usbWifiBusy()) {
    wifiBootConnect();
    gWifiBootTried = true;
    gWifiLastTryMs = millis();
    usbPoll();
  }

  channelsPrime();
  LOGLN("GPIO: boot short=recipe, hold 3s=re-pair. d0/d1/d2 maintained.");
}

void loop() {
  const unsigned long now = millis();
  usbPoll();
  channelsPoll(now);

  if (WiFi.status() != WL_CONNECTED) {
    if (gWifiWasUp) {
      gWifiWasUp = false;
      digitalWrite(LED_BUILTIN, LOW);
    }
    if (!gWifiBootTried && !usbWifiBusy()) {
      wifiBootConnect();
      gWifiBootTried = true;
      gWifiLastTryMs = now;
    }
    if (gWifiHaveCreds && !usbWifiBusy() && (now - gWifiLastTryMs >= 10000UL)) {
      gWifiLastTryMs = now;
      LOGLN("WiFi retry");
      wifiRetryStored();
    }
    return;
  }

  if (usbWifiBusy()) {
    return;
  }

  if (!gWifiWasUp) {
    gWifiWasUp = true;
    if (!gHueReady) {
      afterWifiUp();
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      gNeedConsoleSync = true;
    }
  }
}
