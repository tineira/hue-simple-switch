#include <WiFi.h>
#include "config.h"

String gHueBridgeIp;
String gHueAppKey;

#include "hue.h"
#include "hue_discover.h"

// BOOT del XIAO ESP32-C6: GPIO9, activo en LOW.
// Corta = toggle. Mantener 3 s = volver a emparejar la key del Bridge.
static const int kButtonPin = 9;
static const unsigned long kDebounceMs = 50;

static int lastReading = HIGH;
static int stableState = HIGH;
static unsigned long lastChangeMs = 0;
static unsigned long pressStartMs = 0;
static bool longPressHandled = false;

void setup() {
  Serial.begin(115200);
  // USB CDC: sin PC el write() espera al host. 0 = no bloquear el boot.
  Serial.setTxTimeoutMs(0);
  delay(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(kButtonPin, INPUT_PULLUP);
  lastReading = digitalRead(kButtonPin);
  stableState = lastReading;

  Serial.println("hue-simple-switch");
  Serial.printf("SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("WiFi");
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(250);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi failed, status=%d\n", (int)WiFi.status());
    return;
  }

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BUILTIN, HIGH);

  if (!hueEnsureReady()) {
    Serial.println("Hue setup failed — press Bridge button if pairing, check Wi-Fi LAN");
    return;
  }
  Serial.printf("Using Bridge %s\n", gHueBridgeIp.c_str());
  Serial.println("BOOT short: toggle. Hold 3s: re-pair.");

  bool on = false;
  if (hueGetOn(&on)) {
    Serial.printf("Light is %s\n", on ? "on" : "off");
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
  } else {
    Serial.println("Hue GET failed — check HUE_LIGHT_ID in config.h");
  }
}

void loop() {
  const int reading = digitalRead(kButtonPin);
  const unsigned long now = millis();

  if (reading != lastReading) {
    lastChangeMs = now;
    lastReading = reading;
  }

  if ((now - lastChangeMs) < kDebounceMs) {
    return;
  }
  if (reading == stableState) {
    if (stableState == LOW && !longPressHandled && pressStartMs &&
        (now - pressStartMs) >= kLongPressMs) {
      longPressHandled = true;
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Re-pair skipped: WiFi down");
      } else if (hueRePair()) {
        Serial.printf("Using Bridge %s\n", gHueBridgeIp.c_str());
      }
    }
    return;
  }
  stableState = reading;

  if (reading == LOW) {
    pressStartMs = now;
    longPressHandled = false;
    return;
  }

  // Flanco de subida: pulsación corta si no hubo long-press.
  if (longPressHandled) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Toggle skipped: WiFi down");
    return;
  }

  Serial.println("BOOT: toggle");
  bool nowOn = false;
  if (hueToggle(&nowOn)) {
    digitalWrite(LED_BUILTIN, nowOn ? HIGH : LOW);
    Serial.printf("Light is now %s\n", nowOn ? "on" : "off");
  } else {
    Serial.println("Toggle failed");
  }
}
