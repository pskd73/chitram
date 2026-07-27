#pragma once

#include "menu.h"
#include "window.h"

class MenuWindow : public Window {
public:
  MenuWindow(const char *title, MenuItem *items, int count,
             MenuSelectFn onSelect, const char *hint = nullptr,
             const char *icon = nullptr);

  const char *title() const override { return title_; }
  const char *icon() const override { return icon_; }
  void onEnter() override;
  bool onEvent(JoyEvent e) override;
  int scrollContentHeight() const override;
  void drawContent(int originX, int originY) override;

  Menu &menu() { return menu_; }
  const Menu &menu() const { return menu_; }

  int focusedIndex() const { return menu_.focusedIndex(); }
  void setFocusedIndex(int i) { menu_.setFocusedIndex(i); }

private:
  int hintBlockHeight() const;
  int menuContentTop() const; // content-local Y where menu-local 0 starts

  const char *title_;
  const char *hint_;
  const char *icon_;
  Menu menu_;
};
