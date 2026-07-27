#include "status_window.h"

#include "display.h"
#include "settings.h"
#include "ui_text.h"

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
