#include <WiFi.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <ETBus.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "../secrets.example.h"
#warning "Using placeholder secrets. Copy secrets.example.h to secrets.h and edit it before flashing real hardware."
#endif

#define LED_PIN 4
#define LED_COUNT 16
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

static const char* DEVICE_ID = "RGB1";
static const char* DEVICE_NAME = "ET-Bus RGB LED";
static const char* FW_VERSION = "examples1.7-rgb";

CRGB leds[LED_COUNT];
ETBus etbus;

bool lightOn = true;
uint8_t red = 255;
uint8_t green = 255;
uint8_t blue = 255;
uint8_t brightness = 255;
char effect[32] = "solid";
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

  if      (strcmp(effect, "solid") == 0)          fill_solid(leds, LED_COUNT, base);
  else if (strcmp(effect, "white") == 0)          fill_solid(leds, LED_COUNT, CRGB::White);
  else if (strcmp(effect, "warm_white") == 0)     fill_solid(leds, LED_COUNT, CRGB(255, 147, 41));
  else if (strcmp(effect, "cool_white") == 0)     fill_solid(leds, LED_COUNT, CRGB(201, 226, 255));
  else if (strcmp(effect, "night_light") == 0)   { fill_solid(leds, LED_COUNT, CRGB(255, 100, 20)); FastLED.setBrightness(min<uint8_t>(brightness, 50)); }
  else if (strcmp(effect, "rainbow") == 0)       { fill_rainbow(leds, LED_COUNT, hue, 7); hue++; }
  else if (strcmp(effect, "rainbow_cycle") == 0) { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV((hue + (i * 255 / LED_COUNT)) % 255, 255, 255); hue += 2; }
  else if (strcmp(effect, "color_wipe") == 0)    { if (stepNo % (LED_COUNT * 4) == 0) fill_solid(leds, LED_COUNT, CRGB::Black); leds[(stepNo / 4) % LED_COUNT] = base; }
  else if (strcmp(effect, "color_chase") == 0)   { fill_solid(leds, LED_COUNT, CRGB::Black); leds[stepNo % LED_COUNT] = base; }
  else if (strcmp(effect, "color_bounce") == 0)  { fill_solid(leds, LED_COUNT, CRGB::Black); leds[beatsin16(max<uint16_t>(1, speed / 2), 0, LED_COUNT - 1)] = base; }
  else if (strcmp(effect, "cylon") == 0)         { fadeToBlackBy(leds, LED_COUNT, 20); leds[beatsin16(max<uint16_t>(1, speed / 2), 0, LED_COUNT - 1)] = CRGB::Red; }
  else if (strcmp(effect, "scanner") == 0)       { fadeToBlackBy(leds, LED_COUNT, 10); leds[beatsin16(max<uint16_t>(1, speed / 2), 0, LED_COUNT - 1)] = base; }
  else if (strcmp(effect, "theater_chase") == 0) { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = ((i + stepNo) % 3 == 0) ? base : CRGB::Black; }
  else if (strcmp(effect, "twinkle") == 0)       { fadeToBlackBy(leds, LED_COUNT, 10); if (random8() < 80) leds[random16(LED_COUNT)] = base; }
  else if (strcmp(effect, "sparkle") == 0)       { fill_solid(leds, LED_COUNT, CRGB::Black); leds[random16(LED_COUNT)] = CRGB::White; }
  else if (strcmp(effect, "confetti") == 0)      { fadeToBlackBy(leds, LED_COUNT, 10); leds[random16(LED_COUNT)] += CHSV(hue + random8(64), 200, 255); hue++; }
  else if (strcmp(effect, "juggle") == 0)        { fadeToBlackBy(leds, LED_COUNT, 20); for (uint8_t i = 0; i < 8; i++) leds[beatsin16(i + 7, 0, LED_COUNT - 1)] |= CHSV(hue, 200, 255); hue++; }
  else if (strcmp(effect, "pulse") == 0)         { fill_solid(leds, LED_COUNT, base); FastLED.setBrightness((uint16_t)brightness * beatsin8(max<uint16_t>(1, speed / 10), 0, 255) / 255); }
  else if (strcmp(effect, "heartbeat") == 0)     { uint16_t b = beat16(max<uint16_t>(1, speed)); fill_solid(leds, LED_COUNT, base); FastLED.setBrightness(((b < 10000) || (b > 15000 && b < 25000)) ? brightness : min<uint8_t>(brightness, 50)); }
  else if (strcmp(effect, "strobe") == 0)        { fill_solid(leds, LED_COUNT, (stepNo % 2) ? base : CRGB::Black); }
  else if (strcmp(effect, "running_lights") == 0){ for (uint16_t i = 0; i < LED_COUNT; i++) { leds[i] = base; leds[i].nscale8(sin8((i + stepNo) * 16)); } }
  else if (strcmp(effect, "comet") == 0)         { fadeToBlackBy(leds, LED_COUNT, 50); leds[stepNo % LED_COUNT] = base; }
  else if (strcmp(effect, "meteor") == 0)        { fadeToBlackBy(leds, LED_COUNT, 64); for (uint8_t i = 0; i < 5; i++) { uint16_t p = (stepNo + LED_COUNT - i) % LED_COUNT; leds[p] = base; leds[p].nscale8(255 - i * 50); } }
  else if (strcmp(effect, "chase") == 0)         { uint16_t p = stepNo % LED_COUNT; fill_solid(leds, LED_COUNT, CRGB::Black); for (uint8_t i = 0; i < 3; i++) leds[(p + i * 3) % LED_COUNT] = CHSV((hue + i * 80) % 255, 255, 255); hue++; }
  else if (strcmp(effect, "fire") == 0)          { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV(random8(0, 30), 255, random8(100, 255)); }
  else if (strcmp(effect, "aurora") == 0)        { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV(sin8((i * 10) + hue) + 100, 200, 255); hue++; }
  else if (strcmp(effect, "plasma") == 0)        { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV(sin8((i * 16) + hue) + sin8((i * 8) + hue * 2), 255, 255); hue++; }
  else if (strcmp(effect, "pride") == 0)         { static uint16_t sPt = 0, sLm = 0, sH = 0; uint16_t ms = (uint16_t)millis(), dm = ms - sLm; sLm = ms; sPt += dm * max<uint16_t>(1, speed / 10); sH += dm * 8; uint16_t bt = sPt; for (uint16_t i = 0; i < LED_COUNT; i++) { uint8_t h8 = (sH + (i * 256)) / 256; bt += 32; uint8_t b8 = (uint8_t)(((uint32_t)(sin16(bt) + 32768) * 255) / 65535); leds[i] = CHSV(h8, 255, b8); } }
  else if (strcmp(effect, "lava") == 0)          { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV(random8(0, 30), 255, sin8((i * 8) + hue) + sin8((i * 4) + hue * 3)); hue++; }
  else if (strcmp(effect, "ocean") == 0)         { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV(160, 255, sin8((i * 10) + hue) + sin8(hue * 2)); hue++; }
  else if (strcmp(effect, "christmas") == 0)     { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = ((i + stepNo) % 2) ? CRGB::Red : CRGB::Green; }
  else if (strcmp(effect, "halloween") == 0)     { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = ((i + stepNo) % 2) ? CRGB::Orange : CRGB::Purple; }
  else if (strcmp(effect, "police") == 0)        { uint16_t h = LED_COUNT / 2; for (uint16_t i = 0; i < h; i++) leds[i] = CRGB::Red; for (uint16_t i = h; i < LED_COUNT; i++) leds[i] = CRGB::Blue; if (stepNo % 10 == 0) for (uint16_t i = 0; i < LED_COUNT / 2; i++) { CRGB t = leds[i]; leds[i] = leds[LED_COUNT - 1 - i]; leds[LED_COUNT - 1 - i] = t; } }
  else if (strcmp(effect, "ambulance") == 0)     { fill_solid(leds, LED_COUNT, (stepNo % 2) ? CRGB::Red : CRGB::White); }
  else if (strcmp(effect, "party") == 0)         { for (uint16_t i = 0; i < LED_COUNT; i++) leds[i] = CHSV(random8(), 255, 255); }
  else                                           { strcpy(effect, "solid"); fill_solid(leds, LED_COUNT, base); }

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
  etbus.onSync(publishState);

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
