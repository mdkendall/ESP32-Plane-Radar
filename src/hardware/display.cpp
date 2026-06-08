#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(config::kDisplayRotation);
  tft.clearOuterArea(config::kColorBlack);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
