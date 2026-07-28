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

// Shared word-wrap stepper. Calls onLineEnd each time a line completes
// (before advancing to the next). Returns false from onLineEnd to stop.
template <typename Fn>
static const char *walkWrapped(const char *s, int maxW, uint8_t size,
                               Fn onLineEnd) {
  if (!s || !*s || maxW <= 0) {
    return s;
  }
  const int cw = textCharW(size);
  const char *p = s;
  int lineWidth = 0;

  auto endLine = [&](const char *at) -> bool {
    lineWidth = 0;
    return onLineEnd(at);
  };

  while (*p) {
    if (*p == '\n') {
      ++p;
      if (!endLine(p)) {
        return p;
      }
      continue;
    }

    const char *ws = p;
    int wlen = 0;
    while (ws[wlen] && ws[wlen] != ' ' && ws[wlen] != '\n') {
      wlen++;
    }
    if (wlen == 0 && *ws == ' ') {
      wlen = 1;
    }
    if (wlen < 1) {
      break;
    }

    int wpx = wordPixelWidth(ws, wlen, size);
    if (wpx > maxW) {
      int fit = maxW / cw;
      if (fit < 1) {
        fit = 1;
      }
      if (fit > wlen) {
        fit = wlen;
      }
      wlen = fit;
      wpx = wordPixelWidth(ws, wlen, size);
    }

    if (lineWidth > 0 && (lineWidth + wpx > maxW) && *ws != ' ') {
      if (!endLine(ws)) {
        return ws;
      }
      if (*ws == ' ') {
        p = ws + 1;
        continue;
      }
      // Retry word on the new line (lineWidth is 0).
      continue;
    }

    lineWidth += wpx;
    p = ws + wlen;
  }
  return p;
}

int Text::wrappedLineCount(const char *s, int maxW, uint8_t size) {
  if (!s || !*s) {
    return 1;
  }
  int lines = 1;
  walkWrapped(s, maxW, size, [&](const char *at) {
    (void)at;
    ++lines;
    return true;
  });
  // walkWrapped calls onLineEnd for each completed line before the next;
  // trailing content without a final break already counted as line 1.
  // If text ends with \n, we over-count by 1 empty line — acceptable.
  return lines > 0 ? lines : 1;
}

int Text::wrappedHeight(const char *s, int maxW, uint8_t size) {
  return wrappedLineCount(s, maxW, size) * textLineH(size);
}

const char *Text::skipWrappedLines(const char *s, int maxW, uint8_t size,
                                    int linesToSkip, int *skippedOut) {
  if (skippedOut) {
    *skippedOut = 0;
  }
  if (!s || linesToSkip <= 0) {
    return s;
  }
  int skipped = 0;
  const char *p = walkWrapped(s, maxW, size, [&](const char *at) {
    ++skipped;
    if (skippedOut) {
      *skippedOut = skipped;
    }
    return skipped < linesToSkip;
  });
  return p ? p : s;
}

TextMetrics Text::measure(const char *s, int x, int y, int maxW,
                           const TextStyle &style) {
  return layout(s, x, y, maxW, style, false);
}

TextMetrics Text::draw(const char *s, int x, int y, int maxW,
                        const TextStyle &style) {
  return layout(s, x, y, maxW, style, true);
}

TextMetrics Text::layout(const char *s, int x, int y, int maxW,
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
  const int maxLines = style.maxLines > 0 ? style.maxLines : 0;

  int clipTop = style.clipTop;
  int clipBottom = style.clipBottom;
  if (doDraw && uiClipActive()) {
    if (clipTop < 0 || clipTop < uiClipTop()) {
      clipTop = uiClipTop();
    }
    if (clipBottom < 0 || clipBottom > uiClipBottom()) {
      clipBottom = uiClipBottom();
    }
  }

  // Viewport window: skip wrapped lines until the first line sits fully
  // below clipTop. Never draw straddling lines — that leaves a junk strip
  // under the title bar when only the content rect is blitted.
  const char *drawS = s;
  int cursorY = y;
  int linesSkipped = 0;
  if (doDraw && clipTop >= 0 && cursorY < clipTop) {
    int need = (clipTop - cursorY + lh - 1) / lh; // ceil
    if (need < 1) {
      need = 1;
    }
    int skipped = 0;
    drawS = skipWrappedLines(s, maxW, style.size, need, &skipped);
    linesSkipped = skipped;
    cursorY = y + skipped * lh;
    // If still slightly above (rounding), skip one more via layout loop.
    if (!drawS || !*drawS) {
      m.lines = linesSkipped;
      m.endY = cursorY;
      m.nextY = cursorY + lh;
      m.height = m.nextY - y;
      return m;
    }
  }

  if (doDraw) {
    tft.setTextSize(style.size);
    tft.setTextColor(style.color);
    tft.setTextWrap(false);
  }

  int cursorX = x;
  int line = linesSkipped;
  int lineWidth = 0;
  bool stopped = false;
  const char *p = drawS;

  auto finishLineWidth = [&]() {
    if (lineWidth > m.width) {
      m.width = lineWidth;
    }
  };

  auto advanceLine = [&]() {
    finishLineWidth();
    line++;
    cursorX = x;
    cursorY += lh;
    lineWidth = 0;
  };

  auto lineVisible = [&]() -> bool {
    // Require the full glyph row below the clip — no title-edge straddling.
    if (clipTop >= 0 && cursorY < clipTop) {
      return false;
    }
    if (clipBottom >= 0 && cursorY >= clipBottom) {
      return false;
    }
    if (cursorY < 0 || cursorY > 320) {
      return false;
    }
    return true;
  };

  auto drawEllipsis = [&]() {
    int ex = cursorX;
    if (ex + ellipsisW > x + maxW) {
      ex = x + maxW - ellipsisW;
      if (ex < x) {
        ex = x;
      }
    }
    if (doDraw && lineVisible()) {
      tft.setCursor((int16_t)ex, (int16_t)cursorY);
      tft.print("...");
    }
    cursorX = ex + ellipsisW;
    lineWidth = cursorX - x;
  };

  while (*p && !stopped) {
    if (doDraw && clipBottom >= 0 && cursorY >= clipBottom) {
      break;
    }
    if (maxLines > 0 && line >= maxLines + linesSkipped) {
      if (truncate) {
        drawEllipsis();
      }
      break;
    }

    if (*p == '\n') {
      if (maxLines > 0 && line + 1 >= maxLines + linesSkipped) {
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
      if (maxLines > 0 && line + 1 >= maxLines + linesSkipped) {
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
        tft.setCursor((int16_t)cursorX, (int16_t)cursorY);
        for (int i = 0; i < room; ++i) {
          tft.write(ws[i]);
        }
      }
      cursorX += room * cw;
      lineWidth += room * cw;
      if (truncate) {
        drawEllipsis();
      }
      stopped = true;
      break;
    }

    if (doDraw && lineVisible()) {
      tft.setCursor((int16_t)cursorX, (int16_t)cursorY);
      for (int i = 0; i < wlen; ++i) {
        tft.write(ws[i]);
      }
    }
    cursorX += wpx;
    lineWidth += wpx;
    p = ws + wlen;
  }

  finishLineWidth();
  m.lines = line + (stopped && lineWidth == 0 ? 0 : 1);
  if (m.lines < 1 && *s) {
    m.lines = 1;
  }
  m.endX = cursorX;
  m.endY = cursorY;
  m.nextY = cursorY + lh;
  // Full block height from original origin (include skipped lines above).
  if (doDraw && linesSkipped > 0) {
    // Height of remaining unknown — callers should use wrappedHeight().
    m.height = m.nextY - y;
  } else {
    m.height = m.nextY - y;
  }
  return m;
}

void textDrawCenteredHint(const char *s, int contentTop, uint8_t size,
                            uint16_t color) {
  if (!s || !s[0]) {
    return;
  }
  TextStyle st;
  st.size = size;
  st.color = color;
  st.flags = TextFlagNoWrap;
  st.clipTop = (int16_t)contentTop;
  st.clipBottom = (int16_t)tft.height();
  TextMetrics m = Text::measure(s, 0, 0, tft.width(), st);
  const int viewH = tft.height() - contentTop;
  if (viewH < 1 || m.width < 1) {
    return;
  }
  int x = (tft.width() - m.width) / 2;
  int y = contentTop + (viewH - m.height) / 2;
  if (x < 0) x = 0;
  if (y < contentTop) y = contentTop;
  Text::draw(s, (int16_t)x, (int16_t)y, (int16_t)(tft.width() - x), st);
}
