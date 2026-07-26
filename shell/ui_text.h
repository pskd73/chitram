#pragma once

#include <Arduino.h>

// Adafruit default font: glyph cell ~ (6*size) x (8*size)
inline int textCharW(uint8_t size) { return 6 * (int)size; }
inline int textCharH(uint8_t size) { return 8 * (int)size; }
inline int textLineH(uint8_t size) { return textCharH(size) + 4; }

enum TextFlags : uint16_t {
  TextFlagNone = 0,
  TextFlagWrap = 1 << 0,     // word-wrap (default)
  TextFlagNoWrap = 1 << 1,   // single line (overrides Wrap)
  TextFlagTruncate = 1 << 2, // ellipsis "..." when clipped
};

struct TextStyle {
  uint8_t size = 2;
  uint16_t color = 0xFFFF; // white
  uint16_t flags = TextFlagWrap;
  int maxLines = 0; // 0 = no limit
  // Optional vertical clip in screen px (-1 = none). Skips drawing outside.
  int16_t clipTop = -1;
  int16_t clipBottom = -1;
};

struct TextMetrics {
  int16_t endX = 0;   // after last glyph (or left if line ended empty)
  int16_t endY = 0;   // top of last line
  int16_t nextY = 0;  // endY + lineHeight — place next widget here
  int16_t width = 0;  // widest line drawn/measured
  int16_t height = 0; // total block height
  int16_t lines = 0;
};

// Reusable wrapped text. Use anywhere (title, menus, body, …).
class Text {
public:
  static TextMetrics measure(const char *s, int16_t x, int16_t y, int16_t maxW,
                             const TextStyle &style = TextStyle());

  static TextMetrics draw(const char *s, int16_t x, int16_t y, int16_t maxW,
                          const TextStyle &style = TextStyle());

private:
  static TextMetrics layout(const char *s, int16_t x, int16_t y, int16_t maxW,
                            const TextStyle &style, bool doDraw);
};
