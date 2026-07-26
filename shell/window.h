#pragma once

#include "input.h"
#include <Arduino.h>

static const int kWinTitleH = 32;
static const int kWinStackMax = 8;
static const int kMenuMaxItems = 16;
static const int kUiPadX = 10;
static const int kUiMenuItemPadX = 10;

static const int kUiTitleSize = 2;
static const int kUiBodySize = 2;
static const int kUiMenuSize = 2;
static const int kUiHintSize = 2;

class Window {
public:
  virtual ~Window() {}
  virtual const char *title() const = 0;
  // Optional Icon id for the title bar (left of title). nullptr = none.
  virtual const char *icon() const { return nullptr; }
  // Override to false for fullscreen windows (e.g. photo viewer).
  virtual bool hasTitleBar() const { return true; }
  virtual void onEnter();
  virtual void onExit() {}
  virtual void onFocus() {}
  virtual void onBlur() {}
  // Called every app loop while this window is on top.
  virtual void onTick() {}
  // Return true if handled.
  virtual bool onEvent(JoyEvent e);

  // Total height of scrollable content (content-local px). Override for scroll.
  virtual int scrollContentHeight() const { return viewportHeight(); }

  // Draw content in CONTENT-LOCAL coords: (0,0) = top-left of scrollable area.
  // originX/originY are the screen position of content (0,0) (= contentTop - scrollY).
  // Any drawing at (ox + cx, oy + cy) scrolls automatically.
  // Content is soft-clipped to the viewport — prefer Text/Icon helpers.
  virtual void drawContent(int originX, int originY) = 0;

  virtual void draw();
  void drawChrome();
  void drawContentArea(); // clear viewport + drawContent

  int titleBarHeight() const { return hasTitleBar() ? kWinTitleH : 0; }
  int contentTop() const { return titleBarHeight(); }
  int viewportHeight() const;
  int scrollY() const { return scrollY_; }
  int maxScroll() const;
  void setScrollY(int y);
  void scrollBy(int dy);
  // Keep a content-local rect visible inside the viewport.
  void ensureVisible(int contentY, int height);

  // Screen Y for a content-local Y.
  int toScreenY(int contentY) const {
    return contentTop() + contentY - scrollY_;
  }
  // True if content-local rect intersects the visible viewport.
  bool intersectsViewport(int contentY, int height) const;

protected:
  int scrollY_ = 0;
  int scrollStep() const;
};

class TextWindow : public Window {
public:
  TextWindow(const char *title, const char *body, const char *icon = nullptr);
  const char *title() const override { return title_; }
  const char *icon() const override { return icon_; }
  void onEnter() override;
  bool onEvent(JoyEvent e) override;
  int scrollContentHeight() const override;
  void drawContent(int originX, int originY) override;

private:
  const char *title_;
  const char *body_;
  const char *icon_;
};

struct MenuItem {
  const char *label;
  int id;
  const char *icon; // optional left Icon id (nullable)
  bool selected;    // when true, draw check on the right
};

using MenuSelectFn = void (*)(int index, const MenuItem &item);

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

  int focusedIndex() const { return focus_; }
  void setFocusedIndex(int i);

private:
  int hintBlockHeight() const;
  int rowContentTop(int index) const; // content-local
  int rowHeight() const { return 8 * kUiMenuSize + 10; }
  void paintRow(int index, bool focused); // screen-space, clipped
  void focusChanged(int prev);

  const char *title_;
  MenuItem *items_;
  int count_;
  MenuSelectFn onSelect_;
  const char *hint_;
  const char *icon_;
  int focus_ = 0;
};

class WindowStack {
public:
  void clear();
  void push(Window *w);
  bool pop();
  // Replace the top window(s) with w. n=1 swaps the top; n=2 drops top+under, etc.
  // Depth becomes (oldDepth - n + 1). Root is never removed (n clamped).
  void replaceTop(Window *w, int n = 1);
  Window *top() const;
  int depth() const { return depth_; }
  void dispatch(JoyEvent e);
  void tick();
  void redraw();

private:
  Window *stack_[kWinStackMax] = {};
  int depth_ = 0;
};

extern WindowStack gWindows;
