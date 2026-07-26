#include "ui_text.h"
#include "display.h"
#include "ui_clip.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

static bool flagSet(uint16_t flags, uint16_t f) { return (flags & f) != 0; }

static int wordPixelWidth(const char *start, int len, uint8_t size) {
  (void)start;
  return len * textCharW(size);
}

TextMetrics Text::measure(const char *s, int16_t x, int16_t y, int16_t maxW,
                           const TextStyle &style) {
  return layout(s, x, y, maxW, style, false);
}

TextMetrics Text::draw(const char *s, int16_t x, int16_t y, int16_t maxW,
                        const TextStyle &style) {
  return layout(s, x, y, maxW, style, true);
}

TextMetrics Text::layout(const char *s, int16_t x, int16_t y, int16_t maxW,
                          const TextStyle &style, bool doDraw) {
  TextMetrics m;
  m.endX = x;
  m.endY = y;
  m.nextY = y;
  m.width = 0;
  m.height = 0;
  m.lines = 0;

  if (!s || !*s || maxW <= 0) {
    return m;
  }

  const bool doWrap = !flagSet(style.flags, TextFlagNoWrap);
  const bool truncate = flagSet(style.flags, TextFlagTruncate);
  const int cw = textCharW(style.size);
  const int lh = textLineH(style.size);
  const int ellipsisW = 3 * cw;
  const int maxLines = style.maxLines > 0 ? style.maxLines : 10000;

  if (doDraw) {
    tft.setTextSize(style.size);
    tft.setTextColor(style.color);
  }

  int16_t cursorX = x;
  int16_t cursorY = y;
  int line = 0;
  int lineWidth = 0;
  bool stopped = false;

  auto finishLineWidth = [&]() {
    if (lineWidth > m.width) {
      m.width = (int16_t)lineWidth;
    }
  };

  auto advanceLine = [&]() {
    finishLineWidth();
    line++;
    cursorX = x;
    cursorY = (int16_t)(cursorY + lh);
    lineWidth = 0;
  };

  // Prefer explicit style clip; otherwise inherit active content clip.
  int16_t clipTop = style.clipTop;
  int16_t clipBottom = style.clipBottom;
  if (doDraw && uiClipActive()) {
    if (clipTop < 0 || clipTop < uiClipTop()) {
      clipTop = uiClipTop();
    }
    if (clipBottom < 0 || clipBottom > uiClipBottom()) {
      clipBottom = uiClipBottom();
    }
  }

  auto lineVisible = [&]() -> bool {
    const int ch = textCharH(style.size);
    if (clipTop >= 0 && cursorY + ch <= clipTop) {
      return false;
    }
    if (clipBottom >= 0 && cursorY >= clipBottom) {
      return false;
    }
    return true;
  };

  auto drawEllipsis = [&]() {
    int16_t ex = cursorX;
    if (ex + ellipsisW > x + maxW) {
      ex = (int16_t)(x + maxW - ellipsisW);
      if (ex < x) {
        ex = x;
      }
    }
    if (doDraw && lineVisible()) {
      tft.setCursor(ex, cursorY);
      tft.print("...");
    }
    cursorX = (int16_t)(ex + ellipsisW);
    lineWidth = cursorX - x;
  };

  const char *p = s;
  while (*p && !stopped) {
    if (*p == '\n') {
      if (line + 1 >= maxLines) {
        if (truncate) {
          drawEllipsis();
        }
        stopped = true;
        break;
      }
      advanceLine();
      ++p;
      continue;
    }

    const char *ws = p;
    int wlen = 0;
    if (doWrap) {
      while (ws[wlen] && ws[wlen] != ' ' && ws[wlen] != '\n') {
        wlen++;
      }
      if (wlen == 0 && *ws == ' ') {
        wlen = 1;
      }
    } else {
      wlen = 1;
    }

    int wpx = wordPixelWidth(ws, wlen, style.size);

    if (doWrap && wpx > maxW && wlen > 1) {
      int fit = maxW / cw;
      if (fit < 1) {
        fit = 1;
      }
      wlen = fit;
      wpx = wordPixelWidth(ws, wlen, style.size);
    }

    const bool needsNewLine =
        doWrap && lineWidth > 0 && (lineWidth + wpx > maxW) && *ws != ' ';

    if (needsNewLine) {
      if (line + 1 >= maxLines) {
        if (truncate) {
          drawEllipsis();
        }
        stopped = true;
        break;
      }
      advanceLine();
      if (*ws == ' ') {
        p = ws + 1;
        continue;
      }
    }

    if (lineWidth + wpx > maxW) {
      int room = (maxW - lineWidth) / cw;
      if (truncate) {
        room = (maxW - lineWidth - ellipsisW) / cw;
      }
      if (room < 0) {
        room = 0;
      }
      if (room > wlen) {
        room = wlen;
      }
      if (doDraw && room > 0 && lineVisible()) {
        tft.setCursor(cursorX, cursorY);
        for (int i = 0; i < room; ++i) {
          tft.write(ws[i]);
        }
      }
      cursorX = (int16_t)(cursorX + room * cw);
      lineWidth += room * cw;
      if (truncate) {
        drawEllipsis();
      }
      stopped = true;
      break;
    }

    if (doDraw && lineVisible()) {
      tft.setCursor(cursorX, cursorY);
      for (int i = 0; i < wlen; ++i) {
        tft.write(ws[i]);
      }
    }
    cursorX = (int16_t)(cursorX + wpx);
    lineWidth += wpx;
    p = ws + wlen;
  }

  finishLineWidth();
  m.lines = (int16_t)(line + (stopped && lineWidth == 0 ? 0 : 1));
  if (m.lines < 1 && *s) {
    m.lines = 1;
  }
  m.endX = cursorX;
  m.endY = cursorY;
  m.nextY = (int16_t)(cursorY + lh);
  m.height = (int16_t)(m.nextY - y);
  return m;
}
