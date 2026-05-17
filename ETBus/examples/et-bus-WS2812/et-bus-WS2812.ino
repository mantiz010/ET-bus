#include <WiFi.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <ETBus.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "../../examples1.05/secrets.example.h"
#warning "Using placeholder secrets. Copy ../../examples1.05/secrets.example.h to secrets.h and edit it before flashing real hardware."
#endif

#define LED_PIN 4
#define LED_COUNT 16
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

static const char* DEVICE_ID = "RGB1";
static const char* DEVICE_NAME = "RGB Ring 1";
static const char* FW_VERSION = "ws2812-main-1.05";

CRGB leds[LED_COUNT];
ETBus etbus;

bool lightOn = true;
uint8_t red = 255;
uint8_t green = 255;
uint8_t blue = 255;
uint8_t brightness = 255;
char effect[24] = "solid";
uint8_t speed = 120;
uint8_t hue = 0;
uint16_t stepNo = 0;

static uint16_t frameIntervalMs() {
  const uint16_t ms = map(speed, 1, 255, 120, 12);
  return ms < 10 ? 10 : ms;
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    if (millis() - start > 20000) {
      ESP.restart();
    }
  }
}

static void renderLight() {
  if (!lightOn) {
    fill_solid(leds, LED_COUNT, CRGB::Black);
    FastLED.show();
    return;
  }

  FastLED.setBrightness(brightness);
  const CRGB base = CRGB(red, green, blue);

  if (strcmp(effect, "rainbow") == 0) {
    fill_rainbow(leds, LED_COUNT, hue++, 7);
  } else if (strcmp(effect, "cylon") == 0) {
    fadeToBlackBy(leds, LED_COUNT, 30);
    leds[beatsin16(max<uint16_t>(1, speed / 2), 0, LED_COUNT - 1)] = base;
  } else if (strcmp(effect, "confetti") == 0) {
    fadeToBlackBy(leds, LED_COUNT, 12);
    leds[random16(LED_COUNT)] += CHSV(hue++ + random8(64), 200, 255);
  } else if (strcmp(effect, "chase") == 0) {
    fill_solid(leds, LED_COUNT, CRGB::Black);
    leds[stepNo % LED_COUNT] = base;
  } else if (strcmp(effect, "sparkle") == 0) {
    fill_solid(leds, LED_COUNT, CRGB::Black);
    leds[random16(LED_COUNT)] = base;
  } else if (strcmp(effect, "pulse") == 0) {
    fill_solid(leds, LED_COUNT, base);
    const uint8_t wave = (sin((stepNo % 256) * 6.2831853 / 255.0) + 1.0) * 127.5;
    FastLED.setBrightness((uint16_t)brightness * wave / 255);
  } else {
    strcpy(effect, "solid");
    fill_solid(leds, LED_COUNT, base);
  }

  FastLED.show();
  stepNo++;
}

static void publishState() {
  StaticJsonDocument<256> doc;
  JsonObject payload = doc.to<JsonObject>();

  payload["on"] = lightOn;
  payload["r"] = red;
  payload["g"] = green;
  payload["b"] = blue;
  payload["brightness"] = brightness;
  payload["effect"] = effect;
  payload["speed"] = speed;

  etbus.sendState(payload);
}

static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  if (!dev_class || strcmp(dev_class, "light.rgb") != 0) return;

  if (payload.containsKey("on")) lightOn = (bool)payload["on"];
  if (payload.containsKey("r")) red = constrain((int)payload["r"], 0, 255);
  if (payload.containsKey("g")) green = constrain((int)payload["g"], 0, 255);
  if (payload.containsKey("b")) blue = constrain((int)payload["b"], 0, 255);
  if (payload.containsKey("brightness")) brightness = constrain((int)payload["brightness"], 0, 255);
  if (payload.containsKey("speed")) speed = constrain((int)payload["speed"], 1, 255);

  if (payload.containsKey("effect")) {
    const char* value = payload["effect"] | "solid";
    strlcpy(effect, value, sizeof(effect));
  }

  renderLight();
  publishState();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
  FastLED.clear(true);

  connectWiFi();

  if (strlen(ETBUS_PSK_HEX) == 64) {
    etbus.enableEncryptionHex(ETBUS_PSK_HEX);
  }

  etbus.begin(DEVICE_ID, "light.rgb", DEVICE_NAME, FW_VERSION);
  etbus.onCommand(onEtbusCommand);

  renderLight();
  publishState();
}

void loop() {
  etbus.loop();

  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      ESP.restart();
    }
  }

  static uint32_t lastFrame = 0;
  if (millis() - lastFrame > frameIntervalMs()) {
    lastFrame = millis();
    renderLight();
  }

  static uint32_t lastState = 0;
  if (millis() - lastState > 30000) {
    lastState = millis();
    publishState();
  }
}
