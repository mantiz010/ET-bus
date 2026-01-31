#include <WiFi.h>
#include <ETBus.h>
#include <FastLED.h>

// -----------------------------
// HARD-CODED WIFI
// -----------------------------
static const char* WIFI_SSID = "mantiz010";
static const char* WIFI_PASS = "DavidCross010";

// -----------------------------
// WS2812B CONFIG
// -----------------------------
#define LED_PIN      4         // change to your data pin
#define LED_COUNT    16         // <-- YOU SAID 15 LEDs
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

CRGB leds[LED_COUNT];

// -----------------------------
// ET-BUS
// -----------------------------
ETBus etbus;

// Light state
static bool     isOn = true;
static uint8_t  R = 255, G = 255, B = 255;
static uint8_t  brightness = 255;

// Effects
static char     effect[16] = "solid";  // "solid", "rainbow", "cylon", "confetti", "pulse"
static uint8_t  speedVal = 120;        // 1..255 (higher=faster)

// Timing
static uint32_t lastAnimMs = 0;
static uint16_t animStep = 0;

// -----------------------------
// WIFI CONNECT
// -----------------------------
static void connectWiFi() {
  Serial.println();
  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\n[WIFI] timeout, rebooting");
      delay(300);
      ESP.restart();
    }
  }

  Serial.println("\n[WIFI] Connected");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI] RSSI: ");
  Serial.println(WiFi.RSSI());
}

// -----------------------------
// APPLY OUTPUT (solid only)
// -----------------------------
static void applySolid() {
  CRGB c = isOn ? CRGB(R, G, B) : CRGB::Black;
  for (int i = 0; i < LED_COUNT; i++) leds[i] = c;
  FastLED.setBrightness(isOn ? brightness : 0);
  FastLED.show();
}

// -----------------------------
// EFFECTS ENGINE (non-blocking)
// -----------------------------
static uint16_t speedToIntervalMs(uint8_t s) {
  // map 1..255 -> ~60ms..8ms
  uint16_t ms = 60 - (uint16_t)((s * 52UL) / 255UL);
  if (ms < 8) ms = 8;
  return ms;
}

static void animRainbow() {
  for (int i = 0; i < LED_COUNT; i++) {
    leds[i] = CHSV((uint8_t)(animStep + (i * 256 / LED_COUNT)), 255, 255);
  }
  FastLED.setBrightness(isOn ? brightness : 0);
  FastLED.show();
}

static void animCylon() {
  fadeToBlackBy(leds, LED_COUNT, 40);
  int pos = animStep % (2 * (LED_COUNT - 1));
  if (pos >= (LED_COUNT - 1)) pos = (2 * (LED_COUNT - 1)) - pos;

  leds[pos] = isOn ? CRGB(R, G, B) : CRGB::Black;
  FastLED.setBrightness(isOn ? brightness : 0);
  FastLED.show();
}

static void animConfetti() {
  fadeToBlackBy(leds, LED_COUNT, 30);
  int p = random16(LED_COUNT);
  leds[p] += isOn ? CRGB(R, G, B) : CRGB::Black;
  FastLED.setBrightness(isOn ? brightness : 0);
  FastLED.show();
}

static void animPulse() {
  uint8_t wave = sin8((uint8_t)animStep);
  uint8_t bri = (uint16_t)brightness * wave / 255;
  CRGB c = isOn ? CRGB(R, G, B) : CRGB::Black;
  for (int i = 0; i < LED_COUNT; i++) leds[i] = c;
  FastLED.setBrightness(isOn ? bri : 0);
  FastLED.show();
}

static void runAnimation() {
  if (!isOn) {
    applySolid();
    return;
  }

  if (strcmp(effect, "solid") == 0) {
    applySolid();
    return;
  }

  FastLED.setBrightness(brightness);

  if (strcmp(effect, "rainbow") == 0) animRainbow();
  else if (strcmp(effect, "cylon") == 0) animCylon();
  else if (strcmp(effect, "confetti") == 0) animConfetti();
  else if (strcmp(effect, "pulse") == 0) animPulse();
  else {
    strcpy(effect, "solid");
    applySolid();
  }
}

// -----------------------------
// REPORT STATE (to HA)
// Uses ETBus::sendState() so we can include effect + speed.
// -----------------------------
static void publishState() {
  StaticJsonDocument<192> d;
  JsonObject p = d.to<JsonObject>();

  p["on"] = isOn;
  p["r"] = (int)R;
  p["g"] = (int)G;
  p["b"] = (int)B;
  p["brightness"] = (int)brightness;

  // extra keys (HA can ignore; useful for your UI + future light.py effect support)
  p["effect"] = effect;
  p["speed"] = (int)speedVal;

  etbus.sendState(p);
}

// -----------------------------
// ET-BUS COMMAND HANDLER
// -----------------------------
static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  Serial.print("[ETBUS] command class=");
  Serial.println(dev_class ? dev_class : "(null)");

  if (payload.containsKey("on")) {
    isOn = (bool)payload["on"];
    Serial.print("[ETBUS] on=");
    Serial.println(isOn ? "true" : "false");
  }

  if (payload.containsKey("r")) { R = (uint8_t)((int)payload["r"]); }
  if (payload.containsKey("g")) { G = (uint8_t)((int)payload["g"]); }
  if (payload.containsKey("b")) { B = (uint8_t)((int)payload["b"]); }

  if (payload.containsKey("brightness")) {
    brightness = (uint8_t)((int)payload["brightness"]);
  }

  if (payload.containsKey("effect")) {
    const char* e = payload["effect"];
    if (e && *e) {
      strncpy(effect, e, sizeof(effect) - 1);
      effect[sizeof(effect) - 1] = 0;
    }
  }

  if (payload.containsKey("speed")) {
    int s = (int)payload["speed"];
    if (s < 1) s = 1;
    if (s > 255) s = 255;
    speedVal = (uint8_t)s;
  }

  // reset animation pacing
  lastAnimMs = 0;
  animStep = 0;

  // immediate feedback
  runAnimation();
  publishState();
  Serial.println("[ETBUS] state sent");
}

// -----------------------------
// SETUP
// -----------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=== ET-Bus WS2812B RGB + FX ===");

  // LEDs
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
  FastLED.clear(true);
  FastLED.setBrightness(brightness);

  connectWiFi();

  // ET-Bus init
  etbus.begin(
    "rgb1",        // device id
    "light.rgb",   // class
    "RGB Ring 1",  // name
    "fw-1.1"
  );

  etbus.onCommand(onEtbusCommand);
  Serial.println("[ETBUS] command handler attached");

  applySolid();
  publishState();
}

// -----------------------------
// LOOP
// -----------------------------
void loop() {
  etbus.loop();

  // WiFi keep-alive
  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] lost, reconnecting");
      connectWiFi();
      publishState();
    }
  }

  // Non-blocking animation
  uint16_t interval = speedToIntervalMs(speedVal);
  if (millis() - lastAnimMs >= interval) {
    lastAnimMs = millis();
    animStep++;
    runAnimation();
  }

  // Periodic state refresh (keeps HA + your dashboard in sync)
  static uint32_t lastState = 0;
  if (millis() - lastState > 30000) {
    lastState = millis();
    publishState();
    Serial.println("[ETBUS] periodic state refresh");
  }
}
