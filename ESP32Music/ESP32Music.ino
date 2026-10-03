#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID = "Your SSID";
const char* WIFI_PASSWORD = "Your Password";

// ============================================================
// RGB LED
// ============================================================

#define LED_PIN 48
#define LED_COUNT 1

Adafruit_NeoPixel pixel(
  LED_COUNT,
  LED_PIN,
  NEO_GRB + NEO_KHZ800
);

// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);

// ============================================================
// MUSIC INPUT
// ============================================================

float bass = 0.0;
float mid = 0.0;
float treble = 0.0;

float beatPulse = 0.0;

// ============================================================
// RGB STATE
// ============================================================

float targetR = 0.0;
float targetG = 0.0;
float targetB = 0.0;

float currentR = 0.0;
float currentG = 0.0;
float currentB = 0.0;

// ============================================================
// TIMING
// ============================================================

unsigned long lastFrame = 0;

const uint16_t FRAME_TIME = 10;

// ============================================================
// LED TUNING
// ============================================================

// Maximum brightness.
// Lower = easier on the eyes.
const float MAX_BRIGHTNESS = 0.24;

// Minimum visible brightness.
const float MIN_BRIGHTNESS = 0.008;

// How sensitive the LED is to quiet music.
const float SENSITIVITY = 1.45;

// Attack = how quickly LED reacts to new music.
const float ATTACK = 0.34;

// Decay = how quickly LED fades.
const float DECAY = 0.075;

// ============================================================
// SIMPLE GAMMA CURVE
// ============================================================

float enhance(float value) {

  value = constrain(value, 0.0, 1.0);

  // Makes small signals much more visible.
  // Strong signals still remain controlled.
  return pow(value, 0.58);
}

// ============================================================
// MUSIC HTTP ENDPOINT
// ============================================================

void music() {

  if (server.hasArg("bass")) {

    bass = constrain(
      server.arg("bass").toFloat(),
      0.0,
      1.0
    );
  }

  if (server.hasArg("mid")) {

    mid = constrain(
      server.arg("mid").toFloat(),
      0.0,
      1.0
    );
  }

  if (server.hasArg("treble")) {

    treble = constrain(
      server.arg("treble").toFloat(),
      0.0,
      1.0
    );
  }

  if (server.hasArg("beat")) {

    float beat = server.arg("beat").toFloat();

    if (beat > 0.5) {
      beatPulse = 1.0;
    }
  }

  server.send(200, "text/plain", "OK");
}

// ============================================================
// CALCULATE COLOR
// ============================================================

void calculateColor() {

  // ----------------------------------------------------------
  // 1. Increase sensitivity
  // ----------------------------------------------------------

  float b = enhance(bass * SENSITIVITY);
  float m = enhance(mid * SENSITIVITY);
  float t = enhance(treble * SENSITIVITY);

  // ----------------------------------------------------------
  // 2. Frequency → color
  //
  // Bass   = red/orange
  // Mid    = green
  // Treble = blue
  // ----------------------------------------------------------

  float r = b;
  float g = m;
  float bl = t;

  // Controlled cross mixing.
  // Keeps colors richer without washing everything white.

  r += m * 0.10;
  r += t * 0.04;

  g += b * 0.06;
  g += t * 0.12;

  bl += b * 0.05;
  bl += m * 0.15;

  // ----------------------------------------------------------
  // Normalize
  // ----------------------------------------------------------

  float maximum = max(
    r,
    max(g, bl)
  );

  if (maximum > 1.0) {

    r /= maximum;
    g /= maximum;
    bl /= maximum;
  }

  // ----------------------------------------------------------
  // Overall energy
  // ----------------------------------------------------------

  float energy =
    b * 0.50 +
    m * 0.32 +
    t * 0.18;

  energy = constrain(
    energy,
    0.0,
    1.0
  );

  // ----------------------------------------------------------
  // Adaptive brightness
  // ----------------------------------------------------------

  float brightness =
    MIN_BRIGHTNESS +
    energy * energy * MAX_BRIGHTNESS;

  // Beat increases brightness,
  // but only slightly so it doesn't blind you.

  brightness += beatPulse * 0.045;

  brightness = constrain(
    brightness,
    MIN_BRIGHTNESS,
    MAX_BRIGHTNESS
  );

  // ----------------------------------------------------------
  // VERY SUBTLE BEAT COLOR LIFT
  // ----------------------------------------------------------

  float beatMix = beatPulse * 0.10;

  r += beatMix;
  g += beatMix;
  bl += beatMix;

  // Normalize again.

  maximum = max(
    r,
    max(g, bl)
  );

  if (maximum > 1.0) {

    r /= maximum;
    g /= maximum;
    bl /= maximum;
  }

  // ----------------------------------------------------------
  // Final RGB
  // ----------------------------------------------------------

  targetR = r * 255.0 * brightness;
  targetG = g * 255.0 * brightness;
  targetB = bl * 255.0 * brightness;
}

// ============================================================
// UPDATE LED
// ============================================================

void updateLED() {

  if (millis() - lastFrame < FRAME_TIME) {
    return;
  }

  lastFrame = millis();

  calculateColor();

  // ==========================================================
  // DIFFERENT ATTACK / DECAY
  // ==========================================================

  // React quickly when getting brighter.
  // Fade slowly when getting quieter.

  if (targetR > currentR) {

    currentR +=
      (targetR - currentR) * ATTACK;

  } else {

    currentR +=
      (targetR - currentR) * DECAY;
  }

  if (targetG > currentG) {

    currentG +=
      (targetG - currentG) * ATTACK;

  } else {

    currentG +=
      (targetG - currentG) * DECAY;
  }

  if (targetB > currentB) {

    currentB +=
      (targetB - currentB) * ATTACK;

  } else {

    currentB +=
      (targetB - currentB) * DECAY;
  }

  // ==========================================================
  // SEND RGB
  // ==========================================================

  pixel.setPixelColor(
    0,
    pixel.Color(
      constrain((int)currentR, 0, 255),
      constrain((int)currentG, 0, 255),
      constrain((int)currentB, 0, 255)
    )
  );

  pixel.show();

  // ==========================================================
  // BEAT DECAY
  // ==========================================================

  beatPulse *= 0.84;

  if (beatPulse < 0.005) {
    beatPulse = 0.0;
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  // ----------------------------------------------------------
  // LED
  // ----------------------------------------------------------

  pixel.begin();
  pixel.clear();
  pixel.show();

  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP32 MUSIC RGB");
  Serial.println("Reactive LED v2");
  Serial.println("==============================");

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  WiFi.mode(WIFI_STA);

  // Disable WiFi sleep for lower latency.

  WiFi.setSleep(false);

  Serial.print("Connecting to Wi-Fi");

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 60
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  // ----------------------------------------------------------
  // WIFI RESULT
  // ----------------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("Wi-Fi connected!");

    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

  } else {

    Serial.println("Wi-Fi connection FAILED.");

    // Error indication.
    pixel.setPixelColor(
      0,
      pixel.Color(80, 0, 0)
    );

    pixel.show();
  }

  // ----------------------------------------------------------
  // HTTP SERVER
  // ----------------------------------------------------------

  server.on(
    "/music",
    HTTP_GET,
    music
  );

  server.begin();

  Serial.println("HTTP server started");

  Serial.println("==============================");
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  server.handleClient();

  updateLED();
}

