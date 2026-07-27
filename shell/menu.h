#pragma once

#include "input.h"

#include <Arduino.h>

static const int kMenuMaxItems = 16;
static const int kUiMenuItemPadX = 10;

struct MenuItem {
  const char *label;
  int id;
  const char *icon;     // optional left Icon id (nullable)
  bool selected;        // draw check on the right
  const char *subtitle; // optional second line (nullable)
};

using MenuSelectFn = void (*)(int index, const MenuItem &item);

// Reusable menu list widget (not a Window). Host places it via draw(baseScreenY).
class Menu {
public:
  Menu(MenuItem *items = nullptr, int count = 0,
       MenuSelectFn onSelect = nullptr);

  void setItems(MenuItem *items, int count);
  void setSelectFn(MenuSelectFn onSelect);
  void setWrapNavigation(bool wrap); // true: wrap ends; false: clamp
  void setClip(int clipTop, int clipBottom);
  void setPadX(int padX);

  int count() const { return count_; }
  int focusedIndex() const { return focus_; }
  void setFocusedIndex(int i);
  void resetFocus();

  int rowHeight(int index) const;
  int rowTop(int index) const; // menu-local Y of row
  int contentHeight() const;   // total menu-local height

  // baseScreenY = screen Y of menu-local 0.
  void draw(int baseScreenY) const;

  // Up/Down/Left/Right move focus; Ok invokes select. Returns true if handled.
  // focusChanged is set when focus moved (host should scroll/redraw as needed).
  bool onEvent(JoyEvent e, bool *focusChanged = nullptr);

private:
  void paintRow(int index, bool focused, int baseScreenY) const;

  MenuItem *items_ = nullptr;
  int count_ = 0;
  MenuSelectFn onSelect_ = nullptr;
  int focus_ = 0;
  bool wrap_ = true;
  int clipTop_ = 0;
  int clipBottom_ = 320;
  int padX_ = 10;
};
