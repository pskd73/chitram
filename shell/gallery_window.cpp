#include "gallery_window.h"

#include "display.h"
#include "gallery.h"
#include "icon.h"
#include "image_draw.h"
#include "input.h"
#include "status_window.h"
#include "storage.h"
#include "ui_clip.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>

// Visible page is 2×2; content grows downward and scrolls.
static const int kGalCols = 2;
static const int kGalVisRows = 2;
static const int kGalMargin = 6;
static const int kGalGap = 6;

// Cached cover thumbs — avoids JPEG re-decode on scroll.
static const int kThumbCacheN = 8;
static uint16_t *sThumbPx[kThumbCacheN] = {};
static int sThumbAbs[kThumbCacheN];
static uint8_t sThumbAge[kThumbCacheN];
static int sThumbW = 0;
static int sThumbH = 0;
static uint8_t sThumbClock = 0;

static void thumbCacheInvalidate() {
  for (int i = 0; i < kThumbCacheN; ++i) {
    sThumbAbs[i] = -1;
    sThumbAge[i] = 0;
  }
}

static void thumbCacheFree() {
  for (int i = 0; i < kThumbCacheN; ++i) {
    if (sThumbPx[i]) {
      free(sThumbPx[i]);
      sThumbPx[i] = nullptr;
    }
    sThumbAbs[i] = -1;
  }
  sThumbW = sThumbH = 0;
}

static bool thumbCacheEnsure(int w, int h) {
  if (w < 8 || h < 8) {
    return false;
  }
  if (sThumbW == w && sThumbH == h && sThumbPx[0]) {
    return true;
  }
  thumbCacheFree();
  const size_t bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
  for (int i = 0; i < kThumbCacheN; ++i) {
    sThumbPx[i] = (uint16_t *)heap_caps_malloc(
        bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sThumbPx[i]) {
      sThumbPx[i] = (uint16_t *)malloc(bytes);
    }
    if (!sThumbPx[i]) {
      thumbCacheFree();
      return false;
    }
    sThumbAbs[i] = -1;
    sThumbAge[i] = 0;
  }
  sThumbW = w;
  sThumbH = h;
  return true;
}

static int thumbCacheFind(int absIdx) {
  for (int i = 0; i < kThumbCacheN; ++i) {
    if (sThumbAbs[i] == absIdx && sThumbPx[i]) {
      sThumbAge[i] = ++sThumbClock;
      return i;
    }
  }
  return -1;
}

static int thumbCacheSlot(int absIdx) {
  int slot = thumbCacheFind(absIdx);
  if (slot >= 0) {
    return slot;
  }
  // LRU empty or oldest
  slot = 0;
  for (int i = 0; i < kThumbCacheN; ++i) {
    if (sThumbAbs[i] < 0) {
      slot = i;
      break;
    }
    if (sThumbAge[i] < sThumbAge[slot]) {
      slot = i;
    }
  }
  sThumbAbs[slot] = absIdx;
  sThumbAge[slot] = ++sThumbClock;
  return slot;
}

static void drawClippedRect(int x, int y, int w, int h, uint16_t color) {
  int16_t y0, y1;
  if (!uiClipSpan((int16_t)y, (int16_t)h, &y0, &y1)) {
    return;
  }
  reclaimDisplay();
  if (y >= y0 && y < y1) {
    tft.drawFastHLine(x, y, w, color);
  }
  const int bot = y + h - 1;
  if (bot >= y0 && bot < y1) {
    tft.drawFastHLine(x, bot, w, color);
  }
  const int vy0 = y > y0 ? y : y0;
  const int vy1 = (y + h) < y1 ? (y + h) : y1;
  if (vy1 > vy0) {
    tft.drawFastVLine(x, vy0, vy1 - vy0, color);
    tft.drawFastVLine(x + w - 1, vy0, vy1 - vy0, color);
  }
}

class GalleryViewerWindow : public Window {
public:
  void setIndex(int index) { index_ = index; }
  const char *title() const override { return ""; }
  bool hasTitleBar() const override { return false; }
  void onEnter() override { Window::onEnter(); }
  bool onEvent(JoyEvent e) override;
  void drawContent(int originX, int originY) override;

private:
  int index_ = 0;
  void showAt(int index);
};

class GalleryWindow : public Window {
public:
  const char *title() const override { return "Gallery"; }
  const char *icon() const override { return "gallery"; }
  void onEnter() override;
  void onFocus() override;
  void onExit() override;
  bool onEvent(JoyEvent e) override;
  int scrollContentHeight() const override;
  void drawContent(int originX, int originY) override;

private:
  int count_ = 0;
  int focus_ = 0;

  int absIndex(int displayI) const;
  int rowCount() const;
  void cellSize(int *cellW, int *cellH) const;
  void cellContentRect(int displayI, int *cx, int *cy, int *cw, int *ch) const;
  void paintBorder(int displayI, bool focused);
  void paintCell(int displayI, bool focused);
  void focusChanged(int prev);
  const uint16_t *thumbFor(int absIdx, int cw, int ch);
};

static GalleryWindow sGallery;
static GalleryViewerWindow sViewer;

void GalleryViewerWindow::showAt(int index) {
  int n = galleryCount();
  if (n <= 0) {
    return;
  }
  while (index < 0) {
    index += n;
  }
  index_ = index % n;
  drawContentArea();
}

bool GalleryViewerWindow::onEvent(JoyEvent e) {
  if (e == JoyEvent::Left || e == JoyEvent::Up) {
    showAt(index_ - 1);
    return true;
  }
  if (e == JoyEvent::Right || e == JoyEvent::Down) {
    showAt(index_ + 1);
    return true;
  }
  if (e == JoyEvent::Ok) {
    char path[40];
    if (galleryPathAt(index_, path, sizeof(path))) {
      inputLog("photo: menu %s", path);
      // Replace viewer with menu — Back returns to Gallery (minimal stack)
      gWindows.replaceTop(windowPhotoMenu(path));
      return true;
    }
    inputLog("photo: ok but no path idx=%d", index_);
    return true;
  }
  return false;
}

void GalleryViewerWindow::drawContent(int ox, int oy) {
  (void)ox;
  (void)oy;
  char path[40];
  if (!galleryPathAt(index_, path, sizeof(path)) || !drawImageFile(path)) {
    TextStyle st;
    st.size = kUiBodySize;
    st.color = ILI9341_WHITE;
    st.flags = TextFlagWrap;
    Text::draw("Can't load image", (int16_t)kUiPadX, (int16_t)40,
               (int16_t)(tft.width() - 2 * kUiPadX), st);
  }
}

int GalleryWindow::absIndex(int displayI) const {
  return count_ - 1 - displayI;
}

int GalleryWindow::rowCount() const {
  if (count_ <= 0) {
    return 0;
  }
  return (count_ + kGalCols - 1) / kGalCols;
}

void GalleryWindow::cellSize(int *cellW, int *cellH) const {
  const int viewW = tft.width();
  const int viewH = viewportHeight();
  int cw =
      (viewW - 2 * kGalMargin - (kGalCols - 1) * kGalGap) / kGalCols;
  int ch =
      (viewH - 2 * kGalMargin - (kGalVisRows - 1) * kGalGap) / kGalVisRows;
  int chFromW = (cw * 3) / 4;
  if (ch > chFromW) {
    ch = chFromW;
  }
  *cellW = cw;
  *cellH = ch;
}

void GalleryWindow::cellContentRect(int displayI, int *cx, int *cy, int *cw,
                                    int *ch) const {
  cellSize(cw, ch);
  int col = displayI % kGalCols;
  int row = displayI / kGalCols;
  *cx = kGalMargin + col * (*cw + kGalGap);
  *cy = kGalMargin + row * (*ch + kGalGap);
}

int GalleryWindow::scrollContentHeight() const {
  if (count_ <= 0) {
    return viewportHeight();
  }
  int cw, ch;
  cellSize(&cw, &ch);
  int rows = rowCount();
  return kGalMargin * 2 + rows * ch + (rows - 1) * kGalGap;
}

void GalleryWindow::onEnter() {
  Window::onEnter();
  storageBegin();
  galleryEnsureDir();
  // Thumbs live on SD (/gallery/thumbnails/) and are created at save time.
  // Do not rebuild here — decoding every full image blocks first open.
  count_ = galleryCount();
  focus_ = 0;
  thumbCacheInvalidate();
}

void GalleryWindow::onFocus() {
  count_ = galleryCount();
  if (count_ <= 0) {
    focus_ = 0;
  } else if (focus_ >= count_) {
    focus_ = count_ - 1;
  }
  thumbCacheInvalidate();
}

void GalleryWindow::onExit() {
  // Keep cache warm while nested in viewer; free only if leaving gallery stack
  // entirely — viewer is pushed on top, so don't free here.
}

const uint16_t *GalleryWindow::thumbFor(int absIdx, int cw, int ch) {
  if (!thumbCacheEnsure(cw, ch)) {
    return nullptr;
  }
  int slot = thumbCacheFind(absIdx);
  if (slot >= 0) {
    return sThumbPx[slot];
  }
  char path[40];
  char thumb[48];
  const char *src = nullptr;
  if (galleryThumbPathAt(absIdx, thumb, sizeof(thumb))) {
    src = thumb;
  } else if (galleryPathAt(absIdx, path, sizeof(path))) {
    src = path;
  } else {
    return nullptr;
  }
  slot = thumbCacheSlot(absIdx);
  if (!loadImageCoverToBuffer(src, sThumbPx[slot], cw, ch)) {
    sThumbAbs[slot] = -1;
    return nullptr;
  }
  return sThumbPx[slot];
}

void GalleryWindow::paintBorder(int displayI, bool focused) {
  if (displayI < 0 || displayI >= count_) {
    return;
  }
  int cx, cy, cw, ch;
  cellContentRect(displayI, &cx, &cy, &cw, &ch);
  if (!intersectsViewport(cy, ch)) {
    return;
  }
  int x = cx;
  int y = toScreenY(cy);
  uint16_t border = focused ? ILI9341_CYAN : ILI9341_DARKGREY;
  drawClippedRect(x, y, cw, ch, border);
  drawClippedRect(x + 1, y + 1, cw - 2, ch - 2,
                  focused ? ILI9341_CYAN : ILI9341_BLACK);
}

void GalleryWindow::paintCell(int displayI, bool focused) {
  if (displayI < 0 || displayI >= count_) {
    return;
  }
  int cx, cy, cw, ch;
  cellContentRect(displayI, &cx, &cy, &cw, &ch);
  if (!intersectsViewport(cy, ch)) {
    return;
  }
  int x = cx;
  int y = toScreenY(cy);
  const int abs = absIndex(displayI);
  const uint16_t *px = thumbFor(abs, cw, ch);
  if (px) {
    blitRgb565((int16_t)x, (int16_t)y, (int16_t)cw, (int16_t)ch, px);
  } else {
    int16_t y0, y1;
    if (uiClipSpan((int16_t)y, (int16_t)ch, &y0, &y1)) {
      reclaimDisplay();
      tft.fillRect(x, y0, cw, y1 - y0, 0x4208);
    }
  }
  paintBorder(displayI, focused);
}

void GalleryWindow::focusChanged(int prev) {
  int cx, cy, cw, ch;
  cellContentRect(focus_, &cx, &cy, &cw, &ch);
  const int before = scrollY();
  ensureVisible(cy, ch);
  if (scrollY() != before) {
    drawContentArea();
    return;
  }
  // No scroll: only borders change (cells stay put).
  uiClipSet((int16_t)contentTop(), (int16_t)tft.height());
  paintBorder(prev, false);
  paintBorder(focus_, true);
  uiClipClear();
}

bool GalleryWindow::onEvent(JoyEvent e) {
  if (count_ <= 0) {
    return false;
  }
  if (e == JoyEvent::Ok) {
    sViewer.setIndex(absIndex(focus_));
    gWindows.push(&sViewer);
    return true;
  }
  if (e != JoyEvent::Up && e != JoyEvent::Down && e != JoyEvent::Left &&
      e != JoyEvent::Right) {
    return Window::onEvent(e);
  }

  int prev = focus_;
  int col = focus_ % kGalCols;
  int row = focus_ / kGalCols;

  if (e == JoyEvent::Left) {
    if (focus_ > 0) {
      focus_--;
    }
  } else if (e == JoyEvent::Right) {
    if (focus_ + 1 < count_) {
      focus_++;
    }
  } else if (e == JoyEvent::Up) {
    if (row > 0) {
      focus_ -= kGalCols;
    }
  } else if (e == JoyEvent::Down) {
    int next = focus_ + kGalCols;
    if (next < count_) {
      focus_ = next;
    } else if (focus_ + 1 < count_) {
      focus_ = count_ - 1;
    }
  }

  if (focus_ < 0) {
    focus_ = 0;
  }
  if (focus_ >= count_) {
    focus_ = count_ - 1;
  }
  (void)col;
  if (prev != focus_) {
    focusChanged(prev);
  }
  return true;
}

void GalleryWindow::drawContent(int ox, int oy) {
  (void)ox;
  (void)oy;
  if (count_ <= 0) {
    TextStyle st;
    st.size = kUiBodySize;
    st.color = ILI9341_WHITE;
    st.flags = TextFlagWrap;
    Text::draw("No images yet.\n\nAsk will save photos here.",
               (int16_t)kUiPadX, (int16_t)(contentTop() + 16 - scrollY()),
               (int16_t)(tft.width() - 2 * kUiPadX), st);
    return;
  }

  // Only paint cells that intersect the viewport (cache makes this cheap).
  for (int i = 0; i < count_; ++i) {
    int cx, cy, cw, ch;
    cellContentRect(i, &cx, &cy, &cw, &ch);
    if (intersectsViewport(cy, ch)) {
      paintCell(i, i == focus_);
    }
  }
}

Window *windowGallery() { return &sGallery; }
