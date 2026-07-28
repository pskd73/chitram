#include "make_photo_window.h"

#include "config.h"
#include "display.h"
#include "input.h"
#include "menu.h"
#include "net_services.h"
#include "photo_window.h"
#include "stt_input_window.h"
#include "status_window.h"
#include "ui_clip.h"
#include "ui_text.h"
#include "window.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

static MenuItem kDoneMenuItems[] = {
    {"Generate", 0, "image", false, nullptr},
    {"Make again", 1, "image", false, nullptr},
    {"Home", 2, "home", false, nullptr},
};
static const int kDoneMenuN = 3;
static const uint16_t kBubbleBg = 0xE71C;
static const uint16_t kBubbleFg = 0x18C3;
static const uint16_t kBubblePad = 10;
static const int kBubbleRadius = 12;

enum class MakePhase : uint8_t {
  Idle,
  Ready,
};

class MakePhotoWindow : public Window {
public:
  const char *title() const override {
    return sourcePath_[0] ? "Edit" : "Make Photo";
  }
  const char *icon() const override { return "image"; }
  void setSourcePath(const char *path);
  void onEnter() override;
  void onExit() override;
  void onFocus() override;
  void onTick() override;
  bool onEvent(JoyEvent e) override;
  void drawContent(int originX, int originY) override;

private:
  MakePhase phase_ = MakePhase::Idle;
  String transcript_;
  char sourcePath_[48] = {};
  int16_t bubbleY_ = 0;
  int16_t bubbleH_ = 0;
  int16_t menuY_ = 0;
  bool autoListen_ = false;
  Menu doneMenu_;

  static void onListenConfirm(void *ctx, const char *text);
  static void onListenCancel(void *ctx);

  void openListen();
  void doGenerate();
  void goHome();
  void goIdle();
  void setupDoneMenu();
  void paintDoneMenu();
  int16_t measureBubbleHeight(const char *text, int16_t innerW) const;
  void drawBubble(int16_t x, int16_t y, int16_t w, int16_t h, const char *text);
  void paintReady(int ox, int oy);
  void paintIdle(int ox, int oy);
};

static MakePhotoWindow sMakePhoto;

void MakePhotoWindow::onListenConfirm(void *ctx, const char *text) {
  MakePhotoWindow *w = static_cast<MakePhotoWindow *>(ctx);
  String t = text ? text : "";
  t.trim();
  if (!t.length() || t == "(empty)") {
    w->goIdle();
    return;
  }
  w->transcript_ = t;
  w->phase_ = MakePhase::Ready;
  w->setupDoneMenu();
  w->draw();
  inputLog("make: confirmed \"%s\"", t.c_str());
}

void MakePhotoWindow::onListenCancel(void *ctx) {
  MakePhotoWindow *w = static_cast<MakePhotoWindow *>(ctx);
  w->goIdle();
}

void MakePhotoWindow::setupDoneMenu() {
  doneMenu_.setItems(kDoneMenuItems, kDoneMenuN);
  doneMenu_.setWrapNavigation(true);
  doneMenu_.setPadX(kUiPadX);
  doneMenu_.setClip(contentTop(), tft.height());
  doneMenu_.resetFocus();
}

void MakePhotoWindow::paintDoneMenu() {
  doneMenu_.setClip(contentTop(), tft.height());
  doneMenu_.draw(menuY_);
}

void MakePhotoWindow::setSourcePath(const char *path) {
  if (!path || !path[0]) {
    sourcePath_[0] = '\0';
    return;
  }
  strncpy(sourcePath_, path, sizeof(sourcePath_) - 1);
  sourcePath_[sizeof(sourcePath_) - 1] = '\0';
}

int16_t MakePhotoWindow::measureBubbleHeight(const char *text,
                                             int16_t innerW) const {
  TextStyle st;
  st.size = kUiBodySize;
  st.flags = TextFlagWrap;
  TextMetrics m = Text::measure(text, 0, 0, innerW, st);
  int h = m.height + 2 * kBubblePad;
  if (h < 40) {
    h = 40;
  }
  return (int16_t)h;
}

void MakePhotoWindow::drawBubble(int16_t x, int16_t y, int16_t w, int16_t h,
                                 const char *text) {
  reclaimDisplay();
  int16_t y0, y1;
  if (!uiClipSpan(y, h, &y0, &y1)) {
    return;
  }
  if (y >= contentTop() && y + h <= tft.height()) {
    tft.fillRoundRect(x, y, w, h, kBubbleRadius, kBubbleBg);
  } else {
    tft.fillRect(x, y0, w, y1 - y0, kBubbleBg);
  }

  TextStyle st;
  st.size = kUiBodySize;
  st.color = kBubbleFg;
  st.flags = TextFlagWrap;
  st.clipTop = contentTop();
  st.clipBottom = tft.height();
  Text::draw(text, (int16_t)(x + kBubblePad), (int16_t)(y + kBubblePad),
             (int16_t)(w - 2 * kBubblePad), st);
}

void MakePhotoWindow::goIdle() {
  phase_ = MakePhase::Idle;
  transcript_ = "";
  draw();
}

void MakePhotoWindow::openListen() {
  const char *t = sourcePath_[0] ? "Edit" : "Make Photo";
  gWindows.push(windowSttInput(t, "image", onListenConfirm, onListenCancel,
                               this));
}

void MakePhotoWindow::goHome() { gWindows.pop(); }

void MakePhotoWindow::doGenerate() {
  if (!transcript_.length()) {
    drawContentArea();
    return;
  }
  inputLog("make: generate image...");
  gWindows.replaceTop(windowGenerating());
  delay(30);

  char path[48] = {};
  const char *ref = sourcePath_[0] ? sourcePath_ : nullptr;
  char savedSource[48];
  strncpy(savedSource, sourcePath_, sizeof(savedSource) - 1);
  savedSource[sizeof(savedSource) - 1] = '\0';

  bool ok = generateAndShowImage(transcript_, path, sizeof(path), false, ref);
  if (!ok || !path[0]) {
    if (savedSource[0]) {
      gWindows.replaceTop(windowMakePhotoModify(savedSource));
    } else {
      gWindows.replaceTop(windowMakePhoto());
    }
    return;
  }

  gWindows.replaceTop(windowImagePreview(path));
}

void MakePhotoWindow::onEnter() {
  Window::onEnter();
  transcript_ = "";
  phase_ = MakePhase::Idle;
  setupDoneMenu();
  autoListen_ = true;
}

void MakePhotoWindow::onExit() { autoListen_ = false; }

void MakePhotoWindow::onFocus() { draw(); }

void MakePhotoWindow::onTick() {
  if (autoListen_) {
    autoListen_ = false;
    if (gWindows.top() == this) {
      openListen();
    }
  }
}

bool MakePhotoWindow::onEvent(JoyEvent e) {
  if (phase_ == MakePhase::Ready) {
    if (e == JoyEvent::Ok) {
      const int focus = doneMenu_.focusedIndex();
      if (focus == 0) {
        doGenerate();
      } else if (focus == 1) {
        openListen();
      } else {
        goHome();
      }
      return true;
    }
    if (e == JoyEvent::Back) {
      goHome();
      return true;
    }
    bool moved = false;
    if (doneMenu_.onEvent(e, &moved)) {
      if (moved) {
        paintDoneMenu();
      }
      return true;
    }
    return true;
  }

  // Idle
  if (e == JoyEvent::Ok) {
    openListen();
    return true;
  }
  if (e == JoyEvent::Back) {
    return false;
  }
  return false;
}

void MakePhotoWindow::paintIdle(int ox, int oy) {
  (void)ox;
  (void)oy;
  textDrawCenteredHint("select to speak", contentTop(), kUiBodySize,
                       ILI9341_DARKGREY);
}

void MakePhotoWindow::paintReady(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  const int16_t bubbleX = (int16_t)(ox + kUiPadX);
  bubbleY_ = (int16_t)(oy + 12);

  const int16_t innerW = (int16_t)(maxW - 2 * kBubblePad);
  bubbleH_ = measureBubbleHeight(transcript_.c_str(), innerW);

  const int menuBlock = doneMenu_.contentHeight() + 8;
  const int maxBubble = tft.height() - bubbleY_ - menuBlock;
  if (bubbleH_ > maxBubble && maxBubble > 40) {
    bubbleH_ = (int16_t)maxBubble;
  }

  drawBubble(bubbleX, bubbleY_, maxW, bubbleH_, transcript_.c_str());
  menuY_ = (int16_t)(bubbleY_ + bubbleH_ + 12);
  paintDoneMenu();
}

void MakePhotoWindow::drawContent(int ox, int oy) {
  if (phase_ == MakePhase::Ready && transcript_.length()) {
    paintReady(ox, oy);
  } else {
    paintIdle(ox, oy);
  }
}

Window *windowMakePhoto() {
  sMakePhoto.setSourcePath(nullptr);
  return &sMakePhoto;
}

Window *windowMakePhotoModify(const char *path) {
  sMakePhoto.setSourcePath(path);
  return &sMakePhoto;
}
