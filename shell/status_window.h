#pragma once

#include "window.h"

// Simple titled status page (Wi‑Fi connecting, generating, …).
class StatusWindow : public Window {
public:
  void configure(const char *title, const char *icon, const char *line1,
                 const char *line2 = nullptr);
  const char *title() const override { return title_ ? title_ : ""; }
  const char *icon() const override { return icon_; }
  void drawContent(int originX, int originY) override;
  bool onEvent(JoyEvent e) override; // swallow input while status is showing

private:
  const char *title_ = "";
  const char *icon_ = nullptr;
  const char *line1_ = "";
  const char *line2_ = nullptr;
};

Window *windowWifiConnecting();
Window *windowGenerating();
// Fullscreen boot splash: device name, then advances to Home after 5s.
Window *windowBootSplash();
