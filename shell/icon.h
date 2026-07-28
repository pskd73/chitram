#pragma once

#include <Arduino.h>

// 16×16 monochrome PROGMEM icons.
// Usage anywhere in draw code:
//   Icon("home").draw(x, y);
//   Icon("mic").draw(x, y, ILI9341_CYAN);
//   Icon::draw("gear", x, y, fg, bg);
class Icon {
public:
  static constexpr int kSize = 16;

  explicit Icon(const char *id);

  const char *id() const { return id_; }
  bool valid() const { return bitmap_ != nullptr; }
  int width() const { return kSize; }
  int height() const { return kSize; }

  // fg only (transparent background)
  bool draw(int16_t x, int16_t y, uint16_t color = 0xFFFF) const;
  // fg + opaque bg
  bool draw(int16_t x, int16_t y, uint16_t fg, uint16_t bg) const;

  static bool exists(const char *id);
  static bool draw(const char *id, int16_t x, int16_t y,
                   uint16_t color = 0xFFFF);
  static bool draw(const char *id, int16_t x, int16_t y, uint16_t fg,
                   uint16_t bg);

private:
  const char *id_;
  const uint8_t *bitmap_;
};

// Known ids: home, chat/message/ask, mic, record/rec, loading/wait/busy,
// brain/think/thinking, gallery, image, gear, settings, info, about, wifi,
// storage, display, sound, debug, check, back, bin/trash/delete,
// zoom_in/zoomin, zoom_out/zoomout
