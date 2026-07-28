#include "photo_window.h"

#include "make_photo_window.h"
#include "display.h"
#include "gallery.h"
#include "image_draw.h"
#include "input.h"
#include "menu_window.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

static const int kZoomSteps[] = {1, 2, 4};
static const int kZoomStepN = 3;
static const float kPanStep = 0.14f;

class PhotoWindow : public Window {
public:
  void setPath(const char *path);
  void setGalleryIndex(int index);
  void zoomIn();
  bool zoomOutOne();

  const char *title() const override { return ""; }
  bool hasTitleBar() const override { return false; }
  void onEnter() override;
  void onExit() override;
  void onFocus() override;
  void onTick() override;
  bool onEvent(JoyEvent e) override;
  void drawContent(int originX, int originY) override;

private:
  char path_[48] = {};
  int galleryIndex_ = -1; // >=0 → gallery browse mode
  int zoomStep_ = 0;      // index into kZoomSteps
  float panX_ = 0.5f;
  float panY_ = 0.5f;
  bool imageReady_ = false;
  bool loadFailed_ = false;
  bool loadPending_ = false;

  bool isGallery() const { return galleryIndex_ >= 0; }
  int zoom() const { return kZoomSteps[zoomStep_]; }
  bool isZoomed() const { return zoomStep_ > 0; }

  void resetView();
  void beginLoad();
  void panBy(float dx, float dy);
  void showGalleryAt(int index);
  void openMenu();
  bool resolvePath();
  void paintStatus(const char *msg);
};

static PhotoWindow sPhoto;

void PhotoWindow::resetView() {
  zoomStep_ = 0;
  panX_ = 0.5f;
  panY_ = 0.5f;
}

void PhotoWindow::beginLoad() {
  imageReady_ = false;
  loadFailed_ = false;
  loadPending_ = true;
}

void PhotoWindow::paintStatus(const char *msg) {
  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_WHITE;
  st.flags = TextFlagNoWrap;
  const int16_t maxW = (int16_t)tft.width();
  TextMetrics m = Text::measure(msg, 0, 0, maxW, st);
  int16_t x = (int16_t)((tft.width() - m.width) / 2);
  int16_t y = (int16_t)((tft.height() - m.height) / 2);
  if (x < 0) {
    x = 0;
  }
  if (y < 0) {
    y = 0;
  }
  Text::draw(msg, x, y, maxW, st);
}

void PhotoWindow::setPath(const char *path) {
  galleryIndex_ = -1;
  resetView();
  beginLoad();
  if (!path) {
    path_[0] = '\0';
    return;
  }
  strncpy(path_, path, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
}

void PhotoWindow::setGalleryIndex(int index) {
  galleryIndex_ = index;
  resetView();
  beginLoad();
  resolvePath();
}

bool PhotoWindow::resolvePath() {
  if (!isGallery()) {
    return path_[0] != '\0';
  }
  return galleryPathAt(galleryIndex_, path_, sizeof(path_));
}

void PhotoWindow::onEnter() {
  resetView();
  if (isGallery()) {
    resolvePath();
  }
  beginLoad();
  Window::onEnter(); // paints Loading… before decode
}

void PhotoWindow::onExit() {
  imageZoomCacheClear();
  imageReady_ = false;
  loadPending_ = false;
}

void PhotoWindow::onFocus() {
  // Returning from photo menu — keep zoom/pan; image already on screen.
}

void PhotoWindow::onTick() {
  if (!loadPending_ || gWindows.top() != this) {
    return;
  }
  loadPending_ = false;

  if (!path_[0] && !resolvePath()) {
    loadFailed_ = true;
    drawContentArea();
    return;
  }

  inputLog("photo: load %s", path_);
  if (drawImageZoomed(path_, zoom(), panX_, panY_)) {
    imageReady_ = true;
    loadFailed_ = false;
    // drawImageZoomed already painted the panel.
  } else {
    loadFailed_ = true;
    imageReady_ = false;
    drawContentArea();
  }
}

void PhotoWindow::zoomIn() {
  if (!imageReady_ || zoomStep_ >= kZoomStepN - 1) {
    return;
  }
  ++zoomStep_;
  inputLog("photo: zoom %dx", zoom());
  if (gWindows.top() == this) {
    drawContentArea();
  }
}

bool PhotoWindow::zoomOutOne() {
  if (!imageReady_ || zoomStep_ <= 0) {
    return false;
  }
  --zoomStep_;
  if (zoomStep_ == 0) {
    panX_ = 0.5f;
    panY_ = 0.5f;
  }
  inputLog("photo: zoom %dx", zoom());
  if (gWindows.top() == this) {
    drawContentArea();
  }
  return true;
}

void PhotoWindow::panBy(float dx, float dy) {
  if (!imageReady_ || !isZoomed()) {
    return;
  }
  panX_ += dx;
  panY_ += dy;
  if (panX_ < 0.f) {
    panX_ = 0.f;
  }
  if (panX_ > 1.f) {
    panX_ = 1.f;
  }
  if (panY_ < 0.f) {
    panY_ = 0.f;
  }
  if (panY_ > 1.f) {
    panY_ = 1.f;
  }
  drawContentArea();
}

void PhotoWindow::showGalleryAt(int index) {
  int n = galleryCount();
  if (n <= 0) {
    return;
  }
  while (index < 0) {
    index += n;
  }
  galleryIndex_ = index % n;
  resetView();
  imageZoomCacheClear();
  resolvePath();
  beginLoad();
  drawContentArea(); // shows Loading… immediately
}

void PhotoWindow::openMenu() {
  if (!imageReady_) {
    return;
  }
  if (!path_[0] && !resolvePath()) {
    return;
  }
  inputLog("photo: menu %s", path_);
  gWindows.push(windowPhotoMenu(path_));
}

bool PhotoWindow::onEvent(JoyEvent e) {
  if (loadPending_ || !imageReady_) {
    if (e == JoyEvent::Back) {
      loadPending_ = false;
      return false; // allow cancel while loading
    }
    return true; // swallow other input until loaded
  }

  if (e == JoyEvent::Back) {
    if (zoomOutOne()) {
      return true;
    }
    return false; // pop
  }

  if (e == JoyEvent::Ok) {
    openMenu();
    return true;
  }

  if (isZoomed()) {
    if (e == JoyEvent::Left) {
      panBy(-kPanStep, 0.f);
      return true;
    }
    if (e == JoyEvent::Right) {
      panBy(kPanStep, 0.f);
      return true;
    }
    if (e == JoyEvent::Up) {
      panBy(0.f, -kPanStep);
      return true;
    }
    if (e == JoyEvent::Down) {
      panBy(0.f, kPanStep);
      return true;
    }
    return true;
  }

  if (isGallery()) {
    if (e == JoyEvent::Left) {
      showGalleryAt(galleryIndex_ - 1);
      return true;
    }
    if (e == JoyEvent::Right) {
      showGalleryAt(galleryIndex_ + 1);
      return true;
    }
  }
  return true;
}

void PhotoWindow::drawContent(int ox, int oy) {
  (void)ox;
  (void)oy;
  if (!path_[0] && !resolvePath()) {
    paintStatus("Can't load image");
    return;
  }
  if (loadFailed_) {
    paintStatus("Can't load image\n\nok = menu");
    return;
  }
  if (!imageReady_) {
    paintStatus("Loading");
    return;
  }
  if (!drawImageZoomed(path_, zoom(), panX_, panY_)) {
    paintStatus("Can't load image\n\nok = menu");
  }
}

// ---- Photo action menu ----

static char sPhotoMenuPath[48] = {};

static void onPhotoMenuSelect(int index, const MenuItem &item) {
  (void)index;
  switch (item.id) {
  case 1: // Zoom in
    sPhoto.zoomIn();
    gWindows.pop();
    break;
  case 2: // Zoom out
    sPhoto.zoomOutOne();
    gWindows.pop();
    break;
  case 3: // Edit — drop menu + photo
    if (sPhotoMenuPath[0]) {
      gWindows.replaceTop(windowMakePhotoModify(sPhotoMenuPath), 2);
    }
    break;
  case 4: // Delete — drop menu + photo in one pop (no deleted-file flash)
    if (sPhotoMenuPath[0] && galleryDelete(sPhotoMenuPath)) {
      sPhotoMenuPath[0] = '\0';
    }
    gWindows.popN(2);
    break;
  default:
    break;
  }
}

static MenuItem sPhotoMenuItems[] = {
    {"Zoom in", 1, "zoom_in", false, nullptr},
    {"Zoom out", 2, "zoom_out", false, nullptr},
    {"Edit", 3, "image", false, nullptr},
    {"Delete", 4, "bin", false, nullptr},
};

static MenuWindow sPhotoMenu("Photo", sPhotoMenuItems, 4, onPhotoMenuSelect,
                             nullptr, "gallery");

Window *windowPhotoMenu(const char *imagePath) {
  if (!imagePath) {
    sPhotoMenuPath[0] = '\0';
  } else {
    strncpy(sPhotoMenuPath, imagePath, sizeof(sPhotoMenuPath) - 1);
    sPhotoMenuPath[sizeof(sPhotoMenuPath) - 1] = '\0';
  }
  return &sPhotoMenu;
}

Window *windowPhoto(const char *path) {
  sPhoto.setPath(path);
  return &sPhoto;
}

Window *windowPhotoGallery(int index) {
  sPhoto.setGalleryIndex(index);
  return &sPhoto;
}

Window *windowImagePreview(const char *path) { return windowPhoto(path); }
