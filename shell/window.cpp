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

// ---- MenuWindow ----

MenuWindow::MenuWindow(const char *title, MenuItem *items, int count,
                       MenuSelectFn onSelect, const char *hint,
                       const char *icon)
    : title_(title ? title : ""), items_(items), count_(count),
      onSelect_(onSelect), hint_(hint), icon_(icon) {
  if (count_ > kMenuMaxItems) {
    count_ = kMenuMaxItems;
  }
  if (count_ < 0) {
    count_ = 0;
  }
}

void MenuWindow::onEnter() {
  Window::onEnter();
  focus_ = 0;
}

void MenuWindow::setFocusedIndex(int i) {
  if (count_ <= 0) {
    focus_ = 0;
    return;
  }
  while (i < 0) {
    i += count_;
  }
  focus_ = i % count_;
}

int MenuWindow::hintBlockHeight() const {
  if (!hint_ || !hint_[0]) {
    return 0;
  }
  TextStyle st;
  st.size = kUiHintSize;
  st.flags = TextFlagWrap | TextFlagTruncate;
  st.maxLines = 2;
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  TextMetrics m = Text::measure(hint_, 0, 0, maxW, st);
  return m.height + 8;
}

int MenuWindow::rowContentTop(int index) const {
  return 8 + hintBlockHeight() + index * rowHeight();
}

int MenuWindow::scrollContentHeight() const {
  if (count_ <= 0) {
    return 8 + hintBlockHeight() + 8;
  }
  return rowContentTop(count_ - 1) + rowHeight() + 8;
}

void MenuWindow::paintRow(int index, bool focused) {
  if (index < 0 || index >= count_) {
    return;
  }
  const int cy = rowContentTop(index);
  const int rh = rowHeight();
  if (!intersectsViewport(cy, rh)) {
    return;
  }
  reclaimDisplay();
  const int y = toScreenY(cy);
  const int rowW = tft.width() - 2 * kUiPadX;
  uint16_t bg = focused ? 0x3A2A : 0x18C3;
  uint16_t fg = focused ? ILI9341_YELLOW : ILI9341_WHITE;

  // Clip vertically to viewport
  int drawY = y;
  int drawH = rh - 2;
  if (drawY < kWinTitleH) {
    drawH -= (kWinTitleH - drawY);
    drawY = kWinTitleH;
  }
  if (drawY + drawH > tft.height()) {
    drawH = tft.height() - drawY;
  }
  if (drawH <= 0) {
    return;
  }

  // Full row when fully visible; otherwise fill clipped rect (no round)
  if (y >= kWinTitleH && y + rh - 2 <= tft.height()) {
    tft.fillRoundRect(kUiPadX, y, rowW, rh - 2, 4, bg);
    if (focused) {
      tft.drawRoundRect(kUiPadX, y, rowW, rh - 2, 4, ILI9341_CYAN);
    }
  } else {
    tft.fillRect(kUiPadX, drawY, rowW, drawH, bg);
  }

  const char *iconId = items_[index].icon;
  const bool hasIcon = iconId && iconId[0] && Icon::exists(iconId);
  const bool selected = items_[index].selected;
  const int iconPad = hasIcon ? (Icon::kSize + 8) : 0;
  const int checkPad = selected ? (Icon::kSize + 8) : 0;
  const int textX = kUiPadX + kUiMenuItemPadX + iconPad;
  const int textMaxW = rowW - 2 * kUiMenuItemPadX - iconPad - checkPad;

  if (hasIcon) {
    const int iconY = y + ((rh - 2) - Icon::kSize) / 2;
    if (iconY + Icon::kSize > kWinTitleH && iconY < tft.height()) {
      Icon(iconId).draw((int16_t)(kUiPadX + kUiMenuItemPadX), (int16_t)iconY,
                        fg, bg);
    }
  }

  if (selected) {
    const int iconY = y + ((rh - 2) - Icon::kSize) / 2;
    const int checkX =
        kUiPadX + rowW - kUiMenuItemPadX - Icon::kSize;
    if (iconY + Icon::kSize > kWinTitleH && iconY < tft.height()) {
      Icon("check").draw((int16_t)checkX, (int16_t)iconY, fg, bg);
    }
  }

  if (y + 5 >= kWinTitleH && y + 5 < tft.height() && textMaxW > 0) {
    TextStyle st;
    st.size = kUiMenuSize;
    st.color = fg;
    st.flags = TextFlagNoWrap | TextFlagTruncate;
    st.maxLines = 1;
    st.clipTop = kWinTitleH;
    st.clipBottom = tft.height();
    Text::draw(items_[index].label ? items_[index].label : "",
               (int16_t)textX, (int16_t)(y + 5), (int16_t)textMaxW, st);
  }
}

void MenuWindow::focusChanged(int prev) {
  const int before = scrollY_;
  ensureVisible(rowContentTop(focus_), rowHeight());
  if (scrollY_ != before) {
    drawContentArea();
    return;
  }
  uiClipSet(kWinTitleH, (int16_t)tft.height());
  paintRow(prev, false);
  paintRow(focus_, true);
  uiClipClear();
}

bool MenuWindow::onEvent(JoyEvent e) {
  if (count_ <= 0) {
    return Window::onEvent(e);
  }
  if (e == JoyEvent::Up || e == JoyEvent::Left || e == JoyEvent::Down ||
      e == JoyEvent::Right) {
    int prev = focus_;
    if (e == JoyEvent::Up || e == JoyEvent::Left) {
      setFocusedIndex(focus_ - 1);
    } else {
      setFocusedIndex(focus_ + 1);
    }
    if (prev != focus_) {
      focusChanged(prev);
    }
    return true;
  }
  if (e == JoyEvent::Ok) {
    if (onSelect_ && focus_ >= 0 && focus_ < count_) {
      onSelect_(focus_, items_[focus_]);
    }
    return true;
  }
  return false;
}

void MenuWindow::drawContent(int ox, int oy) {
  if (hint_ && hint_[0]) {
    TextStyle st;
    st.size = kUiHintSize;
    st.color = ILI9341_DARKGREY;
    st.flags = TextFlagWrap | TextFlagTruncate;
    st.maxLines = 2;
    st.clipTop = kWinTitleH;
    st.clipBottom = tft.height();
    const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
    Text::draw(hint_, (int16_t)(ox + kUiPadX), (int16_t)(oy + 8), maxW, st);
  }

  for (int i = 0; i < count_; ++i) {
    paintRow(i, i == focus_);
  }
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

bool WindowStack::pop() {
  if (depth_ <= 1) {
    return false;
  }
  Window *leaving = stack_[--depth_];
  stack_[depth_] = nullptr;
  leaving->onExit();
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
