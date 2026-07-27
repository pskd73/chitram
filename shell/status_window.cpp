#include "status_window.h"

#include "ask_window.h"
#include "display.h"
#include "gallery.h"
#include "image_draw.h"
#include "input.h"
#include "menu_window.h"
#include "settings.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

void StatusWindow::configure(const char *title, const char *icon,
                             const char *line1, const char *line2) {
  title_ = title ? title : "";
  icon_ = icon;
  line1_ = line1 ? line1 : "";
  line2_ = line2;
}

bool StatusWindow::onEvent(JoyEvent e) {
  (void)e;
  return true; // block nav while busy
}

void StatusWindow::drawContent(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  int16_t y = (int16_t)(oy + 24);

  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_WHITE;
  st.flags = TextFlagWrap;
  st.clipTop = contentTop();
  st.clipBottom = tft.height();

  TextMetrics m =
      Text::draw(line1_, (int16_t)(ox + kUiPadX), y, maxW, st);
  if (line2_ && line2_[0]) {
    st.color = ILI9341_DARKGREY;
    st.size = 1;
    Text::draw(line2_, (int16_t)(ox + kUiPadX), (int16_t)(m.nextY + 10), maxW,
               st);
  }
}

static StatusWindow sWifi;
static StatusWindow sGenerating;

Window *windowWifiConnecting() {
  sWifi.configure("Wi-Fi", "wifi", "Connecting...", "please wait");
  return &sWifi;
}

Window *windowGenerating() {
  const char *model = settingsImageModelLabel(settingsImageModel());
  sGenerating.configure("Generate", "image", "Generating image...",
                        model && model[0] ? model : settingsImageModel());
  return &sGenerating;
}

class ImagePreviewWindow : public Window {
public:
  void setPath(const char *path);
  const char *title() const override { return ""; }
  bool hasTitleBar() const override { return false; }
  void drawContent(int originX, int originY) override;
  bool onEvent(JoyEvent e) override;

private:
  char path_[48] = {};
};

static ImagePreviewWindow sPreview;

void ImagePreviewWindow::setPath(const char *path) {
  if (!path) {
    path_[0] = '\0';
    return;
  }
  strncpy(path_, path, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
}

void ImagePreviewWindow::drawContent(int ox, int oy) {
  (void)ox;
  (void)oy;
  if (!path_[0] || !drawImageFile(path_)) {
    TextStyle st;
    st.size = kUiBodySize;
    st.color = ILI9341_WHITE;
    st.flags = TextFlagWrap;
    Text::draw("Can't load image\n\nok = back", (int16_t)kUiPadX, 40,
               (int16_t)(tft.width() - 2 * kUiPadX), st);
  }
}

bool ImagePreviewWindow::onEvent(JoyEvent e) {
  if (e == JoyEvent::Ok) {
    if (path_[0]) {
      inputLog("preview: menu %s", path_);
      // Replace preview with menu (Ask/Gallery stays under)
      gWindows.replaceTop(windowPhotoMenu(path_));
      return true;
    }
    return false;
  }
  if (e == JoyEvent::Back) {
    return false; // pop to Ask / gallery
  }
  return true;
}

Window *windowImagePreview(const char *path) {
  sPreview.setPath(path);
  return &sPreview;
}

// ---- Photo action menu (Edit / Delete) ----

static char sPhotoMenuPath[48] = {};

static void onPhotoMenuSelect(int index, const MenuItem &item) {
  (void)index;
  if (!sPhotoMenuPath[0]) {
    return;
  }
  if (item.id == 1) {
    gWindows.replaceTop(windowAskModify(sPhotoMenuPath));
    return;
  }
  if (item.id == 2) {
    if (galleryDelete(sPhotoMenuPath)) {
      sPhotoMenuPath[0] = '\0';
    }
    gWindows.pop();
  }
}

static MenuItem sPhotoMenuItems[] = {
    {"Edit", 1, "image", false, nullptr},
    {"Delete", 2, "bin", false, nullptr},
};

static MenuWindow sPhotoMenu("Photo", sPhotoMenuItems, 2, onPhotoMenuSelect,
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
