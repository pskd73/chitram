#include "window.h"
#include "display.h"
#include "gallery.h"
#include "icon.h"
#include "input.h"
#include "ui_clip.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

WindowStack gWindows;

int Window::viewportHeight() const { return tft.height() - titleBarHeight(); }

int Window::maxScroll() const {
  int m = scrollContentHeight() - viewportHeight();
  return m > 0 ? m : 0;
}

int Window::scrollStep() const { return textLineH(kUiBodySize); }

void Window::setScrollY(int y) {
  if (y < 0) {
    y = 0;
  }
  int mx = maxScroll();
  if (y > mx) {
    y = mx;
  }
  scrollY_ = y;
}

void Window::scrollBy(int dy) { setScrollY(scrollY_ + dy); }

void Window::ensureVisible(int contentY, int height) {
  if (height < 1) {
    height = 1;
  }
  const int viewH = viewportHeight();
  if (contentY < scrollY_) {
    setScrollY(contentY);
  }
  int bottom = contentY + height;
  if (bottom > scrollY_ + viewH) {
    setScrollY(bottom - viewH);
  }
}

bool Window::intersectsViewport(int contentY, int height) const {
  int sy = toScreenY(contentY);
  int top = contentTop();
  int bot = tft.height();
  return sy + height > top && sy < bot;
}

void Window::onEnter() { scrollY_ = 0; }

bool Window::onEvent(JoyEvent e) {
  // Default: scroll when content overflows
  if (maxScroll() <= 0) {
    return false;
  }
  if (e == JoyEvent::Up || e == JoyEvent::Left) {
    if (scrollY_ <= 0) {
      return false;
    }
    scrollBy(-scrollStep());
    drawContentArea();
    return true;
  }
  if (e == JoyEvent::Down || e == JoyEvent::Right) {
    if (scrollY_ >= maxScroll()) {
      return false;
    }
    scrollBy(scrollStep());
    drawContentArea();
    return true;
  }
  return false;
}

void Window::drawChrome() {
  if (!hasTitleBar()) {
    return;
  }
  reclaimDisplay();
  const int w = tft.width();
  const uint16_t chromeBg = 0x2104;
  tft.fillRect(0, 0, w, kWinTitleH, chromeBg);
  tft.drawFastHLine(0, kWinTitleH - 1, w, 0x8410);

  int textX = kUiPadX;
  const char *iconId = icon();
  if (iconId && iconId[0] && Icon::exists(iconId)) {
    const int iconY = (kWinTitleH - Icon::kSize) / 2;
    Icon(iconId).draw((int16_t)kUiPadX, (int16_t)iconY, ILI9341_WHITE,
                      chromeBg);
    textX += Icon::kSize + 6;
  }

  TextStyle st;
  st.size = kUiTitleSize;
  st.color = ILI9341_WHITE;
  st.flags = TextFlagNoWrap | TextFlagTruncate;
  st.maxLines = 1;
  Text::draw(title(), (int16_t)textX, 8, (int16_t)(w - textX - kUiPadX), st);
}

void Window::drawContentArea() {
  reclaimDisplay();
  const int w = tft.width();
  const int h = tft.height();
  const int top = contentTop();
  if (hasTitleBar()) {
    // Leave title bar pixels alone
    tft.fillRect(0, top, w, h - top, ILI9341_BLACK);
  } else {
    tft.fillScreen(ILI9341_BLACK);
  }
  uiClipSet((int16_t)top, (int16_t)h);
  drawContent(0, top - scrollY_);
  uiClipClear();
}

void Window::draw() {
  if (hasTitleBar()) {
    drawChrome();
  }
  drawContentArea();
}

// ---- TextWindow ----

TextWindow::TextWindow(const char *title, const char *body, const char *icon)
    : title_(title ? title : ""), body_(body ? body : ""),
      icon_(icon) {}

void TextWindow::onEnter() {
  Window::onEnter();
}

bool TextWindow::onEvent(JoyEvent e) {
  return Window::onEvent(e);
}

int TextWindow::scrollContentHeight() const {
  TextStyle st;
  st.size = kUiBodySize;
  st.flags = TextFlagWrap;
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  TextMetrics m = Text::measure(body_, 0, 0, maxW, st);
  return 12 + m.height + 12;
}

void TextWindow::drawContent(int ox, int oy) {
  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_WHITE;
  st.flags = TextFlagWrap;
  st.maxLines = 0;
  st.clipTop = kWinTitleH;
  st.clipBottom = tft.height();
  const int16_t x = (int16_t)(ox + kUiPadX);
  const int16_t y = (int16_t)(oy + 12);
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  Text::draw(body_, x, y, maxW, st);
}

// ---- WindowStack ----

void WindowStack::clear() {
  while (depth_ > 0) {
    stack_[--depth_]->onExit();
    stack_[depth_] = nullptr;
  }
}

void WindowStack::push(Window *w) {
  if (!w) {
    return;
  }
  if (depth_ >= kWinStackMax) {
    Serial.printf("ERR window stack full (%d) — can't push %s\n", depth_,
                  w->title() ? w->title() : "?");
    return;
  }
  if (depth_ > 0) {
    stack_[depth_ - 1]->onBlur();
  }
  stack_[depth_++] = w;
  w->onEnter();
  w->draw();
}

bool WindowStack::pop() { return popN(1); }

bool WindowStack::popN(int n) {
  if (n < 1 || depth_ <= 1) {
    return false;
  }
  if (n >= depth_) {
    n = depth_ - 1; // never remove root
  }
  for (int i = 0; i < n; ++i) {
    Window *leaving = stack_[--depth_];
    stack_[depth_] = nullptr;
    leaving->onExit();
  }
  Window *now = stack_[depth_ - 1];
  now->onFocus();
  now->draw();
  return true;
}

void WindowStack::replaceTop(Window *w, int n) {
  if (!w || depth_ < 1 || n < 1) {
    return;
  }
  // Keep at least the root window under the replacement
  if (n > depth_) {
    n = depth_;
  }
  if (n == depth_ && depth_ > 1) {
    n = depth_ - 1;
  }

  for (int i = 0; i < n; ++i) {
    Window *leaving = stack_[--depth_];
    stack_[depth_] = nullptr;
    leaving->onExit();
  }
  if (depth_ > 0) {
    stack_[depth_ - 1]->onBlur();
  }
  stack_[depth_++] = w;
  w->onEnter();
  w->draw();
}

Window *WindowStack::top() const {
  return depth_ > 0 ? stack_[depth_ - 1] : nullptr;
}

void WindowStack::redraw() {
  if (Window *w = top()) {
    w->draw();
  }
}

void WindowStack::tick() {
  if (Window *w = top()) {
    w->onTick();
  }
}

void WindowStack::dispatch(JoyEvent e) {
  Window *w = top();
  if (!w || e == JoyEvent::None) {
    return;
  }
  if (e == JoyEvent::Screenshot) {
    // Let the top window claim it first (e.g. photo zoom).
    if (w->onEvent(e)) {
      return;
    }
    char saved[48];
    bool ok = gallerySaveScreenshot(saved, sizeof(saved));
    inputLog(ok ? "screenshot ok %s" : "screenshot failed", ok ? saved : "");
    // Brief on-screen confirm without permanently stacking a window
    if (ok) {
      showStatus("Screenshot", "saved to gallery");
      delay(600);
      redraw();
    }
    return;
  }
  if (e == JoyEvent::Back) {
    if (!w->onEvent(e)) {
      pop();
    }
    return;
  }
  w->onEvent(e);
}
