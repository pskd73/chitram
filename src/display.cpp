#include "display.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_GFX.h>

ChitramTft tft(TFT_CS, TFT_DC, TFT_RST);

static String lastDrawnText;

void displayBegin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(3);
  if (!tft.beginMirror()) {
    Serial.println("WARN display: no screenshot framebuffer");
  }
  tft.fillScreen(ILI9341_BLACK);
}

void reclaimDisplay() {
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.setSPISpeed(TFT_SPI_HZ);
}

void showStatus(const char *line1, const char *line2) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(24, 90);
  tft.print(line1);
  if (line2) {
    tft.setCursor(24, 120);
    tft.print(line2);
  }
}

static void showWrappedBody(const char *body, uint16_t color, int y0) {
  tft.setTextColor(color);
  tft.setTextSize(1);
  const int x0 = 24;
  const int maxChars = 34;
  int y = y0;
  int col = 0;
  tft.setCursor(x0, y);
  for (const char *p = body; *p && y < tft.height() - 28; ++p) {
    char c = *p;
    if (c == '\n' || col >= maxChars) {
      y += 10;
      col = 0;
      tft.setCursor(x0, y);
      if (c == '\n') {
        continue;
      }
    }
    tft.write(c);
    col++;
  }
}

void showWrappedText(const char *title, const char *body) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setCursor(24, 20);
  tft.print(title);
  showWrappedBody(body, ILI9341_WHITE, 50);
}

void showListeningUi() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(24, 16);
  tft.print("Listening...");
  tft.setTextSize(1);
  tft.setCursor(24, 42);
  tft.print("Speak — live STT (Deepgram)");
  tft.setCursor(24, 54);
  tft.print("Hold button to talk");
  tft.fillRect(24, 70, 280, 28, 0x4208);
  tft.setCursor(24, 104);
  tft.print("transcript");
  lastDrawnText = "";
}

void showIdleScreen() {
  showStatus("Ready", "tap talk · hold gallery");
}

void drawVuMeter(int peak) {
  const int x0 = 24, y0 = 70, maxW = 280, h = 28;
  int w = (int)((int32_t)peak * maxW / 8000);
  if (w < 0) {
    w = 0;
  }
  if (w > maxW) {
    w = maxW;
  }
  tft.fillRect(x0, y0, maxW, h, 0x4208);
  uint16_t color = ILI9341_RED;
  if (peak >= 2000) {
    color = ILI9341_GREEN;
  } else if (peak >= 500) {
    color = ILI9341_YELLOW;
  }
  if (w > 0) {
    tft.fillRect(x0, y0, w, h, color);
  }
}

void drawLiveTranscript(const String &finalText, const String &interimText) {
  String combined = finalText;
  if (interimText.length()) {
    if (combined.length()) {
      combined += ' ';
    }
    combined += interimText;
  }
  if (combined == lastDrawnText) {
    return;
  }
  lastDrawnText = combined;

  tft.fillRect(20, 104, 290, 120, ILI9341_BLACK);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(24, 104);
  tft.print("transcript");
  if (combined.length() == 0) {
    return;
  }
  showWrappedBody(combined.c_str(), ILI9341_WHITE, 118);
}

void showGalleryHud(int index, int total) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%d/%d", index + 1, total);
  tft.fillRect(0, 0, 80, 18, ILI9341_BLACK);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(6, 4);
  tft.print(buf);
}

void displayResetTranscriptCache() {
  lastDrawnText = "";
}
