#include "web_window.h"

#include "display.h"
#include "input.h"
#include "server.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
#include <stdio.h>
#include <string.h>


class WebWindow : public Window {
public:
  const char *title() const override { return "Web"; }
  const char *icon() const override { return "wifi"; }
  void onEnter() override;
  void onExit() override;
  void onTick() override;
  bool onEvent(JoyEvent e) override;
  int scrollContentHeight() const override;
  void drawContent(int originX, int originY) override;

private:
  bool ok_ = false;
  char body_[240] = {};
};

static WebWindow sWeb;

void WebWindow::onEnter() {
  Window::onEnter();
  ok_ = apServerStart();
  if (ok_) {
    snprintf(body_, sizeof(body_),
             "Wi-Fi: %s\n"
             "Password: %s\n"
             "\n"
             "Open on phone:\n"
             "%s\n"
             "\n"
             "Back to stop",
             apServerSsid(), apServerPassword(), apServerUrl());
  } else {
    snprintf(body_, sizeof(body_), "Couldn't start server.\n%s",
             apServerStatus());
  }
}

void WebWindow::onExit() { apServerStop(); }

void WebWindow::onTick() { apServerPoll(); }

bool WebWindow::onEvent(JoyEvent e) {
  if (e == JoyEvent::Back) {
    return false;
  }
  return true;
}

int WebWindow::scrollContentHeight() const {
  TextStyle st;
  st.size = kUiBodySize;
  st.flags = TextFlagWrap;
  TextMetrics m =
      Text::measure(body_, 0, 0, (int16_t)(tft.width() - 2 * kUiPadX), st);
  return 24 + m.height + 24;
}

void WebWindow::drawContent(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  int16_t y = (int16_t)(oy + 16);

  TextStyle st;
  st.size = kUiBodySize;
  st.color = ok_ ? ILI9341_WHITE : ILI9341_ORANGE;
  st.flags = TextFlagWrap;
  st.clipTop = contentTop();
  st.clipBottom = tft.height();
  Text::draw(body_, (int16_t)(ox + kUiPadX), y, maxW, st);
}

Window *windowWeb() { return &sWeb; }
