#include "UiOled.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool UiOled::begin() {
  Wire.begin();
  _ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (_ok) { display.clearDisplay(); display.display(); }
  return _ok;
}

void UiOled::draw(const UiState& s, const uint32_t gateCounts[], uint8_t gateCount) {
  if (!_ok) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (s.mode == AppMode::Diag) {
    // --- DIAG obrazovka ---
    display.setCursor(0, 0);
    display.print("DIAG  B");
    display.print((int)s.selectedGate + 1);

    display.setCursor(0, 12);
    display.print("diff:");
    display.print(s.diff);

    display.setCursor(0, 24);
    display.print("peak:");
    display.print(s.diffPeak);

    display.setCursor(0, 36);
    display.print("noise:");
    display.print(s.noise);

    display.setCursor(0, 52);
    display.print("thr:");
    display.print((int)DELTA_ON);
    display.print("  10x=EXIT");

    display.display();
    return;
  }

  // --- RUN obrazovka ---
  display.setCursor(0, 0);
  display.print("RUN ");
  display.print("ARM:");
  display.print(s.armed ? "ON " : "OFF");
  display.print(" G1:");
  display.print(s.gate1Signal ? "OK" : "BR");
  if (s.inIgnore) display.print(" IGN");
  display.print(" S");
  display.print(s.stage);

  // 10 bran (2 sloupce x 5 řádků)
  const uint8_t y0 = 12, dy = 10;
  for (uint8_t i = 0; i < gateCount && i < 10; i++) {
    uint8_t col = (i < 5) ? 0 : 1;
    uint8_t row = (i < 5) ? i : (i - 5);
    uint8_t x = (col == 0) ? 0 : 64;
    uint8_t y = y0 + row * dy;

    display.setCursor(x, y);
    display.print("B");
    display.print(i + 1);
    display.print(":");
    display.print(gateCounts[i]);
  }

  display.display();
}
