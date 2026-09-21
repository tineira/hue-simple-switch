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
static unsigned long gWifiLastTryMs = 0;

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
  // USB CDC: sin PC el write() espera al host. 0 = no bloquear el boot.
  Serial.setTxTimeoutMs(0);
  delay(200);
  usbPoll();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  LOG("hue-simple-switch\n");
  LOG("firmware %s\n", FIRMWARE_VERSION);

  consoleLoadNvs();
  recipesLoad();
  channelsBegin();
  consoleWorkerBegin();

  wifiBootConnect();
  gWifiLastTryMs = millis();
  usbPoll();

  if (WiFi.status() == WL_CONNECTED) {
    gWifiWasUp = true;
    afterWifiUp();
  } else if (!gWifiHaveCreds) {
    LOGLN("WiFi failed — USB Improv + HUESET ready");
  }

  channelsPrime();
  LOGLN("GPIO: boot short=recipe, hold 3s=re-pair. d0/d1/d2 maintained.");
}

void loop() {
  const unsigned long now = millis();
  channelsPoll(now);
  usbPoll();

  if (WiFi.status() != WL_CONNECTED) {
    if (gWifiWasUp) {
      gWifiWasUp = false;
      digitalWrite(LED_BUILTIN, LOW);
    }
    if (gWifiHaveCreds && !usbWifiBusy() && (now - gWifiLastTryMs >= 10000UL)) {
      gWifiLastTryMs = now;
      LOGLN("WiFi retry");
      wifiRetryStored();
    }
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
