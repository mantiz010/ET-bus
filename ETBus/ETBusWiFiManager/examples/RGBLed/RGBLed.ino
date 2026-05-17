/*
 * RGBLed.ino — ET-Bus WiFi Manager + RGB LED Controller
 *
 * First boot → creates "ETBus-RGB" WiFi AP
 * Connect from phone → enter WiFi, PSK, device name
 * 35 LED effects, non-blocking engine, NVS persistence
 *
 * Hold BOOT button during startup to reset.
 */

#include <WiFi.h>
#include <Preferences.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <ETBusWiFiManager.h>
#include <ETBus.h>

// ── Config ──
#define LED_PIN      4
#define NUM_LEDS     16
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define RESET_PIN    0

CRGB leds[NUM_LEDS];

ETBusWiFiManager wm;
ETBus etbus;
Preferences prefs;

// Keep Strings alive so .c_str() pointers stay valid
String devName;
String psk;

// ── State ──
bool ledOn = false;
uint8_t ledR = 255, ledG = 255, ledB = 255;
uint8_t ledBrightness = 255;
String ledEffect = "solid";
uint8_t ledSpeed = 120;

uint8_t gHue = 0;
uint16_t effectStep = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastSaveMs = 0;
static uint32_t lastStateHash = 0;
static StaticJsonDocument<384> g_stateDoc;

// ── Command Queue ──
struct PendingCmd {
    volatile bool has = false;
    bool hasOn = false;         bool on = false;
    bool hasR = false;          uint8_t r = 255;
    bool hasG = false;          uint8_t g = 255;
    bool hasB = false;          uint8_t b = 255;
    bool hasBrightness = false; uint8_t brightness = 255;
    bool hasSpeed = false;      uint8_t speed = 120;
    bool hasEffect = false;     char effect[32];
};
PendingCmd pending;

// ── Helpers ──
static uint32_t hashState() {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
    mix((uint32_t)ledOn); mix((uint32_t)ledR); mix((uint32_t)ledG); mix((uint32_t)ledB);
    mix((uint32_t)ledBrightness); mix((uint32_t)ledSpeed);
    for (size_t i = 0; i < ledEffect.length(); i++) mix((uint32_t)ledEffect[i]);
    return h;
}

void saveStateIfChanged() {
    uint32_t now = millis(), h = hashState();
    if (h == lastStateHash || now - lastSaveMs < 800) return;
    lastStateHash = h; lastSaveMs = now;
    prefs.putBool("led_on", ledOn); prefs.putUChar("led_r", ledR);
    prefs.putUChar("led_g", ledG); prefs.putUChar("led_b", ledB);
    prefs.putUChar("led_bright", ledBrightness);
    prefs.putString("led_effect", ledEffect); prefs.putUChar("led_speed", ledSpeed);
    Serial.println("[STATE] Saved");
}

void restoreState() {
    ledOn = prefs.getBool("led_on", false);
    ledR = prefs.getUChar("led_r", 255); ledG = prefs.getUChar("led_g", 255); ledB = prefs.getUChar("led_b", 255);
    ledBrightness = prefs.getUChar("led_bright", 255);
    ledEffect = prefs.getString("led_effect", "solid"); ledSpeed = prefs.getUChar("led_speed", 120);
    lastStateHash = hashState();
    Serial.printf("[STATE] Restored: ON=%d RGB=(%d,%d,%d) Effect=%s\n", ledOn, ledR, ledG, ledB, ledEffect.c_str());
}

void sendLedState() {
    g_stateDoc.clear();
    JsonObject payload = g_stateDoc.to<JsonObject>();
    payload["on"] = ledOn; payload["r"] = ledR; payload["g"] = ledG; payload["b"] = ledB;
    payload["brightness"] = ledBrightness; payload["effect"] = ledEffect; payload["speed"] = ledSpeed;
    etbus.sendState(payload);
}

static uint16_t frameInterval() { uint16_t ms = map(ledSpeed, 0, 255, 120, 12); return ms < 10 ? 10 : ms; }

// ── LED Effects ──
void renderLedFrame() {
    if (!ledOn) { fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show(); return; }
    FastLED.setBrightness(ledBrightness);
    CRGB bc = CRGB(ledR, ledG, ledB);

    if      (ledEffect == "solid")         fill_solid(leds, NUM_LEDS, bc);
    else if (ledEffect == "white")         fill_solid(leds, NUM_LEDS, CRGB::White);
    else if (ledEffect == "warm_white")    fill_solid(leds, NUM_LEDS, CRGB(255,147,41));
    else if (ledEffect == "cool_white")    fill_solid(leds, NUM_LEDS, CRGB(201,226,255));
    else if (ledEffect == "night_light")  { fill_solid(leds, NUM_LEDS, CRGB(255,100,20)); FastLED.setBrightness(50); }
    else if (ledEffect == "rainbow")      { fill_rainbow(leds, NUM_LEDS, gHue, 7); gHue++; }
    else if (ledEffect == "rainbow_cycle") { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV((gHue+(i*255/NUM_LEDS))%255,255,255); gHue+=2; }
    else if (ledEffect == "color_wipe")   { if(effectStep%(NUM_LEDS*4)==0) fill_solid(leds,NUM_LEDS,CRGB::Black); leds[(effectStep/4)%NUM_LEDS]=bc; effectStep++; }
    else if (ledEffect == "color_chase")  { fill_solid(leds,NUM_LEDS,CRGB::Black); leds[effectStep%NUM_LEDS]=bc; effectStep++; }
    else if (ledEffect == "color_bounce") { fill_solid(leds,NUM_LEDS,CRGB::Black); leds[beatsin16(max<uint16_t>(1,ledSpeed/2),0,NUM_LEDS-1)]=bc; }
    else if (ledEffect == "cylon")        { fadeToBlackBy(leds,NUM_LEDS,20); leds[beatsin16(max<uint16_t>(1,ledSpeed/2),0,NUM_LEDS-1)]=CRGB::Red; }
    else if (ledEffect == "scanner")      { fadeToBlackBy(leds,NUM_LEDS,10); leds[beatsin16(max<uint16_t>(1,ledSpeed/2),0,NUM_LEDS-1)]=bc; }
    else if (ledEffect == "theater_chase") { for(int i=0;i<NUM_LEDS;i++) leds[i]=((i+effectStep)%3==0)?bc:CRGB::Black; effectStep++; }
    else if (ledEffect == "twinkle")      { fadeToBlackBy(leds,NUM_LEDS,10); if(random8()<80) leds[random16(NUM_LEDS)]=bc; }
    else if (ledEffect == "sparkle")      { fill_solid(leds,NUM_LEDS,CRGB::Black); leds[random16(NUM_LEDS)]=CRGB::White; }
    else if (ledEffect == "confetti")     { fadeToBlackBy(leds,NUM_LEDS,10); leds[random16(NUM_LEDS)]+=CHSV(gHue+random8(64),200,255); gHue++; }
    else if (ledEffect == "juggle")       { fadeToBlackBy(leds,NUM_LEDS,20); for(int i=0;i<8;i++) leds[beatsin16(i+7,0,NUM_LEDS-1)]|=CHSV(gHue,200,255); gHue++; }
    else if (ledEffect == "pulse")        { fill_solid(leds,NUM_LEDS,bc); FastLED.setBrightness(beatsin8(max<uint16_t>(1,ledSpeed/10),50,255)); }
    else if (ledEffect == "heartbeat")    { uint16_t b=beat16(max<uint16_t>(1,ledSpeed)); fill_solid(leds,NUM_LEDS,bc); FastLED.setBrightness(((b%65536)<10000||((b%65536)>15000&&(b%65536)<25000))?255:50); }
    else if (ledEffect == "strobe")       { fill_solid(leds,NUM_LEDS,(effectStep%2)?bc:CRGB::Black); effectStep++; }
    else if (ledEffect == "running_lights") { for(int i=0;i<NUM_LEDS;i++){leds[i]=bc;leds[i].nscale8(sin8((i+effectStep)*16));} effectStep++; }
    else if (ledEffect == "comet")        { fadeToBlackBy(leds,NUM_LEDS,50); leds[effectStep%NUM_LEDS]=bc; effectStep++; }
    else if (ledEffect == "meteor")       { fadeToBlackBy(leds,NUM_LEDS,64); for(int i=0;i<5;i++){int p=(int)((uint32_t)(effectStep-i)%NUM_LEDS);leds[p]=bc;leds[p].nscale8(255-i*50);} effectStep++; }
    else if (ledEffect == "chase")        { int p=effectStep%NUM_LEDS; fill_solid(leds,NUM_LEDS,CRGB::Black); for(int i=0;i<3;i++) leds[(p+i*3)%NUM_LEDS]=CHSV((gHue+i*80)%255,255,255); effectStep++; gHue++; }
    else if (ledEffect == "fire")         { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(random8(0,30),255,random8(100,255)); }
    else if (ledEffect == "aurora")       { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(sin8((i*10)+gHue)+100,200,255); gHue++; }
    else if (ledEffect == "plasma")       { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(sin8((i*16)+gHue)+sin8((i*8)+gHue*2),255,255); gHue++; }
    else if (ledEffect == "pride") {
        static uint16_t sPt=0,sLm=0,sH=0; uint16_t ms=(uint16_t)millis(),dm=ms-sLm; sLm=ms;
        sPt+=dm*max<uint16_t>(1,ledSpeed/10); sH+=dm*8; uint16_t bt=sPt;
        for(int i=0;i<NUM_LEDS;i++){uint8_t h8=(sH+(i*256))/256;bt+=32;uint8_t b8=(uint8_t)(((uint32_t)(sin16(bt)+32768)*255)/65535);leds[i]=CHSV(h8,255,b8);}
    }
    else if (ledEffect == "lava")         { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(random8(0,30),255,sin8((i*8)+gHue)+sin8((i*4)+gHue*3)); gHue++; }
    else if (ledEffect == "ocean")        { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(160,255,sin8((i*10)+gHue)+sin8(gHue*2)); gHue++; }
    else if (ledEffect == "christmas")    { for(int i=0;i<NUM_LEDS;i++) leds[i]=((i+effectStep)%2)?CRGB::Red:CRGB::Green; effectStep++; }
    else if (ledEffect == "halloween")    { for(int i=0;i<NUM_LEDS;i++) leds[i]=((i+effectStep)%2)?CRGB::Orange:CRGB::Purple; effectStep++; }
    else if (ledEffect == "police")       { int h=NUM_LEDS/2; for(int i=0;i<h;i++) leds[i]=CRGB::Red; for(int i=h;i<NUM_LEDS;i++) leds[i]=CRGB::Blue; if(effectStep++%10==0){for(int i=0;i<NUM_LEDS/2;i++){CRGB t=leds[i];leds[i]=leds[NUM_LEDS-1-i];leds[NUM_LEDS-1-i]=t;}} }
    else if (ledEffect == "ambulance")    { fill_solid(leds,NUM_LEDS,(effectStep%2)?CRGB::Red:CRGB::White); effectStep++; }
    else if (ledEffect == "party")        { for(int i=0;i<NUM_LEDS;i++) leds[i]=CHSV(random8(),255,255); }
    else fill_solid(leds, NUM_LEDS, bc);

    FastLED.show();
}

void serviceLedEngine() {
    uint32_t now = millis();
    if (now - lastFrameMs < frameInterval()) return;
    lastFrameMs = now;
    renderLedFrame();
}

// ── Command Handler ──
void onCommand(const char* dev_class, JsonObject payload) {
    if (strcmp(dev_class, "light.rgb") != 0) return;
    PendingCmd p; memset(&p, 0, sizeof(p));
    if (payload.containsKey("on"))         { p.hasOn=true; p.on=payload["on"]; }
    if (payload.containsKey("r"))          { p.hasR=true; p.r=(uint8_t)payload["r"]; }
    if (payload.containsKey("g"))          { p.hasG=true; p.g=(uint8_t)payload["g"]; }
    if (payload.containsKey("b"))          { p.hasB=true; p.b=(uint8_t)payload["b"]; }
    if (payload.containsKey("brightness")) { p.hasBrightness=true; p.brightness=(uint8_t)payload["brightness"]; }
    if (payload.containsKey("speed"))      { p.hasSpeed=true; p.speed=(uint8_t)payload["speed"]; }
    if (payload.containsKey("effect"))     { const char*fx=payload["effect"]; if(fx&&fx[0]){p.hasEffect=true;strlcpy(p.effect,fx,sizeof(p.effect));} }
    pending = p; pending.has = true;
}

void processPendingCmd() {
    if (!pending.has) return;
    PendingCmd p = pending; pending.has = false;
    bool changed = false;
    if (p.hasOn)         { ledOn=p.on; changed=true; }
    if (p.hasR)          { ledR=p.r; changed=true; }
    if (p.hasG)          { ledG=p.g; changed=true; }
    if (p.hasB)          { ledB=p.b; changed=true; }
    if (p.hasBrightness) { ledBrightness=p.brightness; changed=true; }
    if (p.hasSpeed)      { ledSpeed=p.speed; changed=true; }
    if (p.hasEffect)     { ledEffect=String(p.effect); effectStep=0; changed=true; }
    if (changed) { saveStateIfChanged(); sendLedState(); }
}

// ── Setup ──
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ET-Bus RGB LED Controller ===");

    prefs.begin("etbus", false);
    restoreState();

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setMaxRefreshRate(60);
    renderLedFrame();

    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        Serial.println("[BOOT] Reset held — clearing settings");
        wm.resetSettings();
    }

    wm.begin("ETBus-RGB");

    Serial.printf("[WIFI] Connected to: %s\n", wm.getSSID().c_str());
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    WiFi.setSleep(false);

    // Store Strings so .c_str() pointers stay valid
    devName = wm.getDevName();
    if (devName.length() == 0) devName = "RGB1";

    psk = wm.getPSK();

    if (psk.length() == 64) {
        if (etbus.enableEncryptionHex(psk.c_str())) {
            Serial.println("[ETBUS] Encrypted (key from portal)");
        }
    } else {
        Serial.println("[ETBUS] No PSK — running unencrypted");
    }

    etbus.begin(devName.c_str(), "light.rgb", "RGB LED Strip", "v1.7");
    etbus.onCommand(onCommand);
    etbus.onSync(sendLedState);
    delay(150);
    sendLedState();

    Serial.println("\n[BOOT] READY\n");
}

// ── Loop ──
void loop() {
    etbus.loop();
    processPendingCmd();
    serviceLedEngine();
    delay(0);
}
