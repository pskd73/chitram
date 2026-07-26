#include "ui_clip.h"

#include "display.h"

#include <Adafruit_ILI9341.h>

static int16_t sTop = -1;
static int16_t sBot = -1;

void uiClipSet(int16_t top, int16_t bottom) {
  sTop = top;
  sBot = bottom;
}

void uiClipClear() {
  sTop = -1;
  sBot = -1;
}

bool uiClipActive() { return sTop >= 0 && sBot > sTop; }

int16_t uiClipTop() { return sTop; }
int16_t uiClipBottom() { return sBot; }

bool uiClipSpan(int16_t y, int16_t h, int16_t *y0, int16_t *y1) {
  if (h <= 0) {
    return false;
  }
  int16_t a = y;
  int16_t b = (int16_t)(y + h);
  if (uiClipActive()) {
    if (a < sTop) {
      a = sTop;
    }
    if (b > sBot) {
      b = sBot;
    }
  } else {
    if (a < 0) {
      a = 0;
    }
    if (b > tft.height()) {
      b = tft.height();
    }
  }
  if (a >= b) {
    return false;
  }
  if (y0) {
    *y0 = a;
  }
  if (y1) {
    *y1 = b;
  }
  return true;
}
