#include "status_window.h"

#include "display.h"
#include "settings.h"
#include "ui_text.h"
#include "windows.h"

#include <Adafruit_ILI9341.h>

void StatusWindow::configure(const char *title, const char *icon,
                             const char *line1, const char *line2) {
  title_ = title ? title : "";
  icon_ = icon;
  line1_ = line1 ? line1 : "";
  line2_ = line2;
}

bool StatusWindow::onEvent(JoyEvent e) {
  (void)e;
  return true; // block nav while busy
}

void StatusWindow::drawContent(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  int16_t y = (int16_t)(oy + 24);

  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_WHITE;
  st.flags = TextFlagWrap;
  st.clipTop = contentTop();
  st.clipBottom = tft.height();

  TextMetrics m =
      Text::draw(line1_, (int16_t)(ox + kUiPadX), y, maxW, st);
  if (line2_ && line2_[0]) {
    st.color = ILI9341_DARKGREY;
    st.size = 1;
    Text::draw(line2_, (int16_t)(ox + kUiPadX), (int16_t)(m.nextY + 10), maxW,
               st);
  }
}

static StatusWindow sWifi;
static StatusWindow sGenerating;

Window *windowWifiConnecting() {
  sWifi.configure("Wi-Fi", "wifi", "Connecting...", "please wait");
  return &sWifi;
}

Window *windowGenerating() {
  const char *model = settingsImageModelLabel(settingsImageModel());
  sGenerating.configure("Generate", "image", "Generating image...",
                        model && model[0] ? model : settingsImageModel());
  return &sGenerating;
}

// ---- Boot splash: big device name for 5s ----

static const uint32_t kBootSplashMs = 5000;
static const uint8_t kBootNameSize = 4;

class BootSplashWindow : public Window {
public:
  const char *title() const override { return ""; }
  bool hasTitleBar() const override { return false; }
  void onEnter() override;
  void onTick() override;
  bool onEvent(JoyEvent e) override;
  void drawContent(int originX, int originY) override;

private:
  uint32_t enteredMs_ = 0;
};

static BootSplashWindow sBootSplash;

void BootSplashWindow::onEnter() {
  Window::onEnter();
  enteredMs_ = millis();
}

void BootSplashWindow::onTick() {
  if ((millis() - enteredMs_) >= kBootSplashMs) {
    gWindows.replaceTop(windowHome());
  }
}

bool BootSplashWindow::onEvent(JoyEvent e) {
  (void)e;
  return true;
}

void BootSplashWindow::drawContent(int ox, int oy) {
  (void)ox;
  (void)oy;
  const char *name = settingsDeviceName();
  if (!name || !name[0]) {
    name = "Chitram";
  }

  TextStyle st;
  st.size = kBootNameSize;
  st.color = ILI9341_WHITE;
  st.flags = TextFlagWrap;
  const int16_t maxW = (int16_t)(tft.width() - 2 * 16);
  TextMetrics m = Text::measure(name, 0, 0, maxW, st);

  int16_t x = (int16_t)((tft.width() - m.width) / 2);
  if (x < 16) {
    x = 16;
  }
  int16_t y = (int16_t)((tft.height() - m.height) / 2);
  if (y < 8) {
    y = 8;
  }
  // Double-draw offset for a heavier "bold" look on the GFX font.
  Text::draw(name, x, y, maxW, st);
  Text::draw(name, (int16_t)(x + 1), y, maxW, st);
}

Window *windowBootSplash() { return &sBootSplash; }

