#include <WiFi.h>
#include <ETBus.h>

// -----------------------------
// WIFI
// -----------------------------
static const char* WIFI_SSID = "home";
static const char* WIFI_PASS = "test";

// -----------------------------
// RGB GPIOs (PWM)
// -----------------------------
static const int PIN_R = 25;
static const int PIN_G = 26;
static const int PIN_B = 27;

// LEDC channels
static const int CH_R = 0;
static const int CH_G = 1;
static const int CH_B = 2;

// -----------------------------
// ET-BUS
// -----------------------------
ETBus etbus;

// State
static bool    isOn = false;
static uint8_t R = 255, G = 255, B = 255;
static uint8_t brightness = 255;

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
// APPLY OUTPUT
// -----------------------------
static void applyRgb() {
  uint8_t sr = isOn ? (uint16_t)R * brightness / 255 : 0;
  uint8_t sg = isOn ? (uint16_t)G * brightness / 255 : 0;
  uint8_t sb = isOn ? (uint16_t)B * brightness / 255 : 0;

  ledcWrite(CH_R, sr);
  ledcWrite(CH_G, sg);
  ledcWrite(CH_B, sb);

  Serial.printf("[RGB] APPLY on=%d R=%d G=%d B=%d bri=%d\n",
                isOn, R, G, B, brightness);
}

// -----------------------------
// REPORT STATE (NEW LIBRARY STYLE)
// -----------------------------
static void publishState() {
  StaticJsonDocument<128> d;
  JsonObject p = d.to<JsonObject>();

  p["on"] = isOn;
  p["r"] = (int)R;
  p["g"] = (int)G;
  p["b"] = (int)B;
  p["brightness"] = (int)brightness;

  etbus.sendState(p);
}

// -----------------------------
// ET-BUS COMMAND HANDLER
// -----------------------------
static void onEtbusCommand(const char* dev_class, JsonObject payload) {
  Serial.print("[ETBUS] command class=");
  Serial.println(dev_class ? dev_class : "(null)");

  if (payload.containsKey("on"))
    isOn = (bool)payload["on"];

  if (payload.containsKey("r"))
    R = (uint8_t)((int)payload["r"]);

  if (payload.containsKey("g"))
    G = (uint8_t)((int)payload["g"]);

  if (payload.containsKey("b"))
    B = (uint8_t)((int)payload["b"]);

  if (payload.containsKey("brightness"))
    brightness = (uint8_t)((int)payload["brightness"]);

  applyRgb();
  publishState();
  Serial.println("[ETBUS] state sent");
}

// -----------------------------
// SETUP
// -----------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n=== ET-Bus RGB Light (Solid) ===");

  // PWM setup
  ledcSetup(CH_R, 5000, 8);
  ledcSetup(CH_G, 5000, 8);
  ledcSetup(CH_B, 5000, 8);
  ledcAttachPin(PIN_R, CH_R);
  ledcAttachPin(PIN_G, CH_G);
  ledcAttachPin(PIN_B, CH_B);

  connectWiFi();

  etbus.begin(
    "rgb2",        // <-- unique device id
    "light.rgb",   // class
    "RGB Light 2", // name
    "fw-1.0"
  );

  etbus.onCommand(onEtbusCommand);
  Serial.println("[ETBUS] command handler attached");

  applyRgb();
  publishState();
}

// -----------------------------
// LOOP
// -----------------------------
void loop() {
  etbus.loop();

  // WiFi keep-alive
  static uint32_t lastWifi = 0;
  if (millis() - lastWifi > 5000) {
    lastWifi = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] reconnecting");
      connectWiFi();
      publishState();
    }
  }

  // Periodic state refresh
  static uint32_t lastState = 0;
  if (millis() - lastState > 30000) {
    lastState = millis();
    publishState();
    Serial.println("[ETBUS] heartbeat state");
  }
}

