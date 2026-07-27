#include "menu.h"

#include "display.h"
#include "icon.h"
#include "ui_clip.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>

static const int kMenuTextSize = 2;
static const int kMenuRowPad = 10;
static const int kMenuSubtitleExtra = 12;

Menu::Menu(MenuItem *items, int count, MenuSelectFn onSelect)
    : items_(items), count_(count), onSelect_(onSelect) {
  if (count_ > kMenuMaxItems) {
    count_ = kMenuMaxItems;
  }
  if (count_ < 0) {
    count_ = 0;
  }
}

void Menu::setItems(MenuItem *items, int count) {
  items_ = items;
  count_ = count;
  if (count_ > kMenuMaxItems) {
    count_ = kMenuMaxItems;
  }
  if (count_ < 0) {
    count_ = 0;
  }
  if (focus_ >= count_) {
    focus_ = count_ > 0 ? count_ - 1 : 0;
  }
}

void Menu::setSelectFn(MenuSelectFn onSelect) { onSelect_ = onSelect; }

void Menu::setWrapNavigation(bool wrap) { wrap_ = wrap; }

void Menu::setClip(int clipTop, int clipBottom) {
  clipTop_ = clipTop;
  clipBottom_ = clipBottom;
}

void Menu::setPadX(int padX) { padX_ = padX; }

void Menu::setFocusedIndex(int i) {
  if (count_ <= 0) {
    focus_ = 0;
    return;
  }
  if (wrap_) {
    while (i < 0) {
      i += count_;
    }
    focus_ = i % count_;
    return;
  }
  if (i < 0) {
    i = 0;
  }
  if (i >= count_) {
    i = count_ - 1;
  }
  focus_ = i;
}

void Menu::resetFocus() { focus_ = 0; }

int Menu::rowHeight(int index) const {
  (void)index;
  const bool sub =
      items_ && index >= 0 && index < count_ && items_[index].subtitle &&
      items_[index].subtitle[0];
  return 8 * kMenuTextSize + kMenuRowPad + (sub ? kMenuSubtitleExtra : 0);
}

int Menu::rowTop(int index) const {
  int y = 0;
  for (int i = 0; i < index && i < count_; ++i) {
    y += rowHeight(i);
  }
  return y;
}

int Menu::contentHeight() const {
  if (count_ <= 0) {
    return 0;
  }
  return rowTop(count_ - 1) + rowHeight(count_ - 1);
}

void Menu::paintRow(int index, bool focused, int baseScreenY) const {
  if (!items_ || index < 0 || index >= count_) {
    return;
  }
  const int rh = rowHeight(index);
  const int y = baseScreenY + rowTop(index);
  if (y + rh <= clipTop_ || y >= clipBottom_) {
    return;
  }

  reclaimDisplay();
  const int rowW = tft.width() - 2 * padX_;
  uint16_t bg = focused ? 0x3A2A : 0x18C3;
  uint16_t fg = focused ? ILI9341_YELLOW : ILI9341_WHITE;
  uint16_t subColor = focused ? 0xC618 : 0x8410;

  int drawY = y;
  int drawH = rh - 2;
  if (drawY < clipTop_) {
    drawH -= (clipTop_ - drawY);
    drawY = clipTop_;
  }
  if (drawY + drawH > clipBottom_) {
    drawH = clipBottom_ - drawY;
  }
  if (drawH <= 0) {
    return;
  }

  if (y >= clipTop_ && y + rh - 2 <= clipBottom_) {
    tft.fillRoundRect(padX_, y, rowW, rh - 2, 4, bg);
    if (focused) {
      tft.drawRoundRect(padX_, y, rowW, rh - 2, 4, ILI9341_CYAN);
    }
  } else {
    tft.fillRect(padX_, drawY, rowW, drawH, bg);
  }

  const MenuItem &item = items_[index];
  const char *iconId = item.icon;
  const bool hasIcon = iconId && iconId[0] && Icon::exists(iconId);
  const bool selected = item.selected;
  const bool hasSub = item.subtitle && item.subtitle[0];
  const int iconPad = hasIcon ? (Icon::kSize + 8) : 0;
  const int checkPad = selected ? (Icon::kSize + 8) : 0;
  const int textX = padX_ + kUiMenuItemPadX + iconPad;
  const int textMaxW = rowW - 2 * kUiMenuItemPadX - iconPad - checkPad;

  if (hasIcon) {
    const int iconY =
        hasSub ? (y + 8) : (y + ((rh - 2) - Icon::kSize) / 2);
    if (iconY + Icon::kSize > clipTop_ && iconY < clipBottom_) {
      Icon(iconId).draw((int16_t)(padX_ + kUiMenuItemPadX), (int16_t)iconY, fg,
                        bg);
    }
  }

  if (selected) {
    const int iconY = y + ((rh - 2) - Icon::kSize) / 2;
    const int checkX = padX_ + rowW - kUiMenuItemPadX - Icon::kSize;
    if (iconY + Icon::kSize > clipTop_ && iconY < clipBottom_) {
      Icon("check").draw((int16_t)checkX, (int16_t)iconY, fg, bg);
    }
  }

  if (textMaxW > 0) {
    TextStyle st;
    st.size = kMenuTextSize;
    st.color = fg;
    st.flags = TextFlagNoWrap | TextFlagTruncate;
    st.maxLines = 1;
    st.clipTop = (int16_t)clipTop_;
    st.clipBottom = (int16_t)clipBottom_;
    const int labelY = hasSub ? (y + 4) : (y + 5);
    if (labelY >= clipTop_ && labelY < clipBottom_) {
      Text::draw(item.label ? item.label : "", (int16_t)textX, (int16_t)labelY,
                 (int16_t)textMaxW, st);
    }
    if (hasSub) {
      st.size = 1;
      st.color = subColor;
      const int subY = y + 4 + 8 * kMenuTextSize + 2;
      if (subY >= clipTop_ && subY < clipBottom_) {
        Text::draw(item.subtitle, (int16_t)textX, (int16_t)subY,
                   (int16_t)textMaxW, st);
      }
    }
  }
}

void Menu::draw(int baseScreenY) const {
  for (int i = 0; i < count_; ++i) {
    paintRow(i, i == focus_, baseScreenY);
  }
}

bool Menu::onEvent(JoyEvent e, bool *focusChanged) {
  if (focusChanged) {
    *focusChanged = false;
  }
  if (count_ <= 0) {
    return false;
  }
  if (e == JoyEvent::Up || e == JoyEvent::Left || e == JoyEvent::Down ||
      e == JoyEvent::Right) {
    int prev = focus_;
    if (e == JoyEvent::Up || e == JoyEvent::Left) {
      if (!wrap_ && focus_ <= 0) {
        return true;
      }
      setFocusedIndex(focus_ - 1);
    } else {
      if (!wrap_ && focus_ >= count_ - 1) {
        return true;
      }
      setFocusedIndex(focus_ + 1);
    }
    if (prev != focus_) {
      if (focusChanged) {
        *focusChanged = true;
      }
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
