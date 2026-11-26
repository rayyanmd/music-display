// Bring-up test for the LCDwiki 2.8" ESP32-S3 display.
// 1) R/G/B fills, so you can verify color order + inversion (should be true red, green, blue)
// 2) Info screen: PSRAM, flash, battery voltage
// 3) Touch to draw; BOOT button cycles the RGB LED; backlight fades in.
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "LGFX_ES3C28P.hpp"

static constexpr int PIN_BOOT = 0;
static constexpr int PIN_RGB = 42;
static constexpr int PIN_BAT_ADC = 9;
static constexpr int PIN_AUDIO_EN = 1;  // low = speaker amp on

LGFX tft;
Adafruit_NeoPixel rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

float batteryVolts() {
  // 200k/200k divider -> pin sees half the battery voltage
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  return (mv / 16.0f) * 2.0f / 1000.0f;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_EN, HIGH);  // keep amp off (avoids pops) until audio is used
  pinMode(PIN_BOOT, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);

  rgb.begin();
  rgb.setBrightness(40);
  rgb.show();

  tft.init();
  tft.setBrightness(0);
  tft.setRotation(1);  // landscape 320x240; 0 = portrait

  const uint16_t cols[] = {TFT_RED, TFT_GREEN, TFT_BLUE};
  for (int i = 0; i < 3; i++) {
    tft.fillScreen(cols[i]);
    for (int b = 0; b <= 255 && i == 0; b += 5) { tft.setBrightness(b); delay(4); }
    tft.setBrightness(255);
    delay(400);
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.drawString("ES3C28P ready", 10, 10);
  tft.setFont(&fonts::Font2);
  tft.setCursor(10, 50);
  tft.printf("PSRAM: %u KB free\n", (unsigned)(ESP.getFreePsram() / 1024));
  tft.setCursor(10, 70);
  tft.printf("Flash: %u MB\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  tft.setCursor(10, 90);
  tft.printf("Battery: %.2f V\n", batteryVolts());
  tft.setCursor(10, 110);
  tft.print("Touch to draw. BOOT = cycle LED.");

  Serial.printf("PSRAM %u bytes, flash %u bytes\n", ESP.getPsramSize(), ESP.getFlashChipSize());
}

void loop() {
  static uint8_t ledState = 0;
  static bool lastBoot = true;

  int32_t x, y;
  if (tft.getTouch(&x, &y)) {
    tft.fillCircle(x, y, 3, TFT_CYAN);
  }

  bool boot = digitalRead(PIN_BOOT);
  if (lastBoot && !boot) {
    ledState = (ledState + 1) % 4;
    const uint32_t c[] = {0, rgb.Color(255, 0, 0), rgb.Color(0, 255, 0), rgb.Color(0, 0, 255)};
    rgb.setPixelColor(0, c[ledState]);
    rgb.show();
  }
  lastBoot = boot;
  delay(5);
}
