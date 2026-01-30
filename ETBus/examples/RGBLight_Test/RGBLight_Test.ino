\
#include <WiFi.h>
#include <ETBus.h>

#include "secrets.h"

// Simple PWM RGB example (change pins to your wiring)
static const int PIN_R = 25;
static const int PIN_G = 26;
static const int PIN_B = 27;

// LEDC channels
static const int CH_R = 0;
static const int CH_G = 1;
static const int CH_B = 2;

ETBus etbus;

static bool isOn = true;
static uint8_t r = 255, g = 255, b = 255;
static uint8_t bri = 255;

static void applyRgb() {
  uint8_t sr = isOn ? (uint16_t)r * bri / 255 : 0;
  uint8_t sg = isOn ? (uint16_t)g * bri / 255 : 0;
  uint8_t sb = isOn ? (uint16_t)b * bri / 255 : 0;

  ledcWrite(CH_R, sr);
  ledcWrite(CH_G, sg);
  ledcWrite(CH_B, sb);

  etbus.sendRgbState(isOn, r, g, b, bri);
}

static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  if (payload.containsKey("on")) isOn = (bool)payload["on"];
  if (payload.containsKey("r")) r = (uint8_t)((int)payload["r"]);
  if (payload.containsKey("g")) g = (uint8_t)((int)payload["g"]);
  if (payload.containsKey("b")) b = (uint8_t)((int)payload["b"]);
  if (payload.containsKey("brightness")) bri = (uint8_t)((int)payload["brightness"]);

  applyRgb();
}

void setup() {
  Serial.begin(115200);

  ledcSetup(CH_R, 5000, 8);
  ledcSetup(CH_G, 5000, 8);
  ledcSetup(CH_B, 5000, 8);
  ledcAttachPin(PIN_R, CH_R);
  ledcAttachPin(PIN_G, CH_G);
  ledcAttachPin(PIN_B, CH_B);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  etbus.begin("rgb1", "light.rgb", "Test RGB 1", "test-1.0");
  etbus.onCommand(onEtbusCommand);

  applyRgb();
}

void loop() {
  etbus.loop();
}
