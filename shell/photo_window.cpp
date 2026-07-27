#include "photo_window.h"

#include "ask_window.h"
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
  bool onEvent(JoyEvent e) override;
  void drawContent(int originX, int originY) override;

private:
  char path_[48] = {};
  int galleryIndex_ = -1; // >=0 → gallery browse mode
  int zoomStep_ = 0;      // index into kZoomSteps
  float panX_ = 0.5f;
  float panY_ = 0.5f;

  bool isGallery() const { return galleryIndex_ >= 0; }
  int zoom() const { return kZoomSteps[zoomStep_]; }
  bool isZoomed() const { return zoomStep_ > 0; }

  void resetView();
  void panBy(float dx, float dy);
  void showGalleryAt(int index);
  void openMenu();
  bool resolvePath();
};

static PhotoWindow sPhoto;

void PhotoWindow::resetView() {
  zoomStep_ = 0;
  panX_ = 0.5f;
  panY_ = 0.5f;
}

void PhotoWindow::setPath(const char *path) {
  galleryIndex_ = -1;
  resetView();
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
  resolvePath();
}

bool PhotoWindow::resolvePath() {
  if (!isGallery()) {
    return path_[0] != '\0';
  }
  return galleryPathAt(galleryIndex_, path_, sizeof(path_));
}

void PhotoWindow::onEnter() {
  Window::onEnter();
  resetView();
  if (isGallery()) {
    resolvePath();
  }
}

void PhotoWindow::onExit() {
  imageZoomCacheClear();
}

void PhotoWindow::onFocus() {
  // Returning from photo menu — keep zoom/pan, just refresh.
}

void PhotoWindow::zoomIn() {
  if (zoomStep_ >= kZoomStepN - 1) {
    return;
  }
  ++zoomStep_;
  inputLog("photo: zoom %dx", zoom());
  if (gWindows.top() == this) {
    drawContentArea();
  }
}

bool PhotoWindow::zoomOutOne() {
  if (zoomStep_ <= 0) {
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
  if (!isZoomed()) {
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
  drawContentArea();
}

void PhotoWindow::openMenu() {
  if (!path_[0] && !resolvePath()) {
    return;
  }
  inputLog("photo: menu %s", path_);
  // Push (don't replace) so zoom/pan state survives under the menu.
  gWindows.push(windowPhotoMenu(path_));
}

bool PhotoWindow::onEvent(JoyEvent e) {
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
    // All directions pan — zoom is via the menu.
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

  // 1× gallery: Left/Right browse photos.
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
    TextStyle st;
    st.size = kUiBodySize;
    st.color = ILI9341_WHITE;
    st.flags = TextFlagWrap;
    Text::draw("Can't load image", (int16_t)kUiPadX, 40,
               (int16_t)(tft.width() - 2 * kUiPadX), st);
    return;
  }
  if (!drawImageZoomed(path_, zoom(), panX_, panY_)) {
    TextStyle st;
    st.size = kUiBodySize;
    st.color = ILI9341_WHITE;
    st.flags = TextFlagWrap;
    Text::draw("Can't load image\n\nok = menu", (int16_t)kUiPadX, 40,
               (int16_t)(tft.width() - 2 * kUiPadX), st);
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
      gWindows.replaceTop(windowAskModify(sPhotoMenuPath), 2);
    }
    break;
  case 4: // Delete — drop menu + photo
    if (sPhotoMenuPath[0] && galleryDelete(sPhotoMenuPath)) {
      sPhotoMenuPath[0] = '\0';
    }
    gWindows.pop(); // menu
    gWindows.pop(); // photo
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
