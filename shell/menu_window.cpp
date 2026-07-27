#include "menu_window.h"

#include "display.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>

MenuWindow::MenuWindow(const char *title, MenuItem *items, int count,
                       MenuSelectFn onSelect, const char *hint,
                       const char *icon)
    : title_(title ? title : ""), hint_(hint), icon_(icon),
      menu_(items, count, onSelect) {
  menu_.setPadX(kUiPadX);
  menu_.setWrapNavigation(true);
}

void MenuWindow::onEnter() {
  Window::onEnter();
  menu_.resetFocus();
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

int MenuWindow::menuContentTop() const { return 8 + hintBlockHeight(); }

int MenuWindow::scrollContentHeight() const {
  return menuContentTop() + menu_.contentHeight() + 8;
}

bool MenuWindow::onEvent(JoyEvent e) {
  if (menu_.count() <= 0) {
    return Window::onEvent(e);
  }
  bool moved = false;
  if (!menu_.onEvent(e, &moved)) {
    return false;
  }
  if (moved) {
    const int fi = menu_.focusedIndex();
    const int rowY = menuContentTop() + menu_.rowTop(fi);
    const int rowH = menu_.rowHeight(fi);
    // Include the leading top pad when bringing the first row into view;
    // otherwise ensureVisible(rowY=8) leaves scrollY=8 and eats the padding.
    if (fi == 0) {
      ensureVisible(0, rowY + rowH);
    } else {
      ensureVisible(rowY, rowH);
    }
    drawContentArea();
  }
  return true;
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

  menu_.setClip(kWinTitleH, tft.height());
  menu_.setPadX(kUiPadX);
  menu_.draw(oy + menuContentTop());
}
