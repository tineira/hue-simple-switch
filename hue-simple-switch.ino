#include <WiFi.h>
#include "config.h"

String gHueBridgeIp;
String gHueAppKey;

#include "hue.h"
#include "hue_discover.h"
#include "recipes.h"
#include "channels.h"
#include "console.h"

static bool gWifiWasUp = false;
static bool gHueReady = false;
static unsigned long gWifiLastTryMs = 0;

static void afterWifiUp() {
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.printf("mac %s\n", deviceMacHex().c_str());
  digitalWrite(LED_BUILTIN, HIGH);
  if (!hueEnsureReady()) {
    Serial.println("Hue setup failed — press Bridge button if pairing, check Wi-Fi LAN");
    gHueReady = false;
    return;
  }
  gHueReady = true;
  Serial.printf("Using Bridge %s id=%s\n", gHueBridgeIp.c_str(), gHueBridgeId.c_str());
  if (recipesBindBridge(gHueBridgeId)) {
    gConsoleRegistered = false;
  }
  if (!consoleConfigured()) {
    Serial.println("console: CONSOLE_URL / CONSOLE_TOKEN not set");
  }
  gNeedConsoleSync = true;
}

void setup() {
  Serial.begin(115200);
  // USB CDC: sin PC el write() espera al host. 0 = no bloquear el boot.
  Serial.setTxTimeoutMs(0);
  delay(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("hue-simple-switch");
  Serial.printf("firmware %s  SSID: %s\n", FIRMWARE_VERSION, WIFI_SSID);

  // Recetas en NVS antes de leer GPIO. La primera muestra no dispara on/off.
  recipesLoad();
  channelsBegin();
  consoleWorkerBegin();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  gWifiLastTryMs = millis();

  Serial.print("WiFi");
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(250);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    gWifiWasUp = true;
    afterWifiUp();
  } else {
    Serial.printf("WiFi failed, status=%d — will retry\n", (int)WiFi.status());
  }
  channelsPrime();
  Serial.println("GPIO: boot short=recipe, hold 3s=re-pair. d0/d1/d2 maintained.");
}

void loop() {
  const unsigned long now = millis();
  channelsPoll(now);

  if (WiFi.status() != WL_CONNECTED) {
    if (gWifiWasUp) {
      gWifiWasUp = false;
      digitalWrite(LED_BUILTIN, LOW);
    }
    if (now - gWifiLastTryMs >= 10000UL) {
      gWifiLastTryMs = now;
      Serial.println("WiFi retry");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
