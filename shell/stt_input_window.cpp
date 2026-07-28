#include "stt_input_window.h"

#include "config.h"
#include "deepgram.h"
#include "display.h"
#include "input.h"
#include "menu.h"
#include "stt_session.h"
#include "ui_clip.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

static const uint16_t kBubbleBg  = 0xE71C;
static const uint16_t kBubbleFg  = 0x18C3;
static const uint16_t kBubblePad = 10;
static const int      kBubbleR   = 12;

class SttInputWindow : public Window {
public:
  void init(const char *title, const char *icon, SttConfirmCb onConfirm,
            SttCancelCb onCancel, void *ctx, const SttInputOpts &opts);

  const char *title() const override { return title_; }
  const char *icon()  const override { return icon_; }
  const char *statusIcon() const override;
  uint16_t statusIconColor() const override;

  void onEnter() override;
  void onExit()  override;
  void onTick()  override;
  bool onEvent(JoyEvent e) override;
  void drawContent(int ox, int oy) override;

private:
  const char *title_ = "Input";
  const char *icon_  = "chat";
  SttConfirmCb confirmCb_ = nullptr;
  SttCancelCb  cancelCb_  = nullptr;
  void *ctx_ = nullptr;
  bool autoConfirm_ = false;
  const char *confirmLabel_ = nullptr;
  const char *rerecordLabel_ = nullptr;
  const char *cancelLabel_ = nullptr;

  String transcript_;
  String lastBubbleText_;
  int16_t bubbleY_ = 0;
  int16_t bubbleH_ = 0;
  int16_t menuY_   = 0;
  uint32_t lastUiMs_ = 0;
  MenuItem confirmItems_[3] = {};
  Menu confirmMenu_;

  static void onSttDone(void *ctx, const String &text);
  static void onSttError(void *ctx, const char *reason);
  static void onSttDraw(void *ctx);

  void startListening();
  void doConfirm();
  void doCancel();

  int16_t measureBubbleH(const char *text, int16_t innerW) const;
  void drawBubble(int16_t x, int16_t y, int16_t w, int16_t h, const char *text);
  void updateBubbleIfNeeded();
  void setupMenu();
  void paintMenu();
  void paintBubbleAndMenu(int ox, int oy);
  void paintConnecting(int ox, int oy);
  void paintListening(int ox, int oy);
  void paintError(int ox, int oy);

  String currentText() const;
};

static SttInputWindow sSttInput;

// ---- callbacks ----

void SttInputWindow::onSttDone(void *ctx, const String &text) {
  SttInputWindow *w = static_cast<SttInputWindow *>(ctx);
  w->transcript_ = text;
  w->lastBubbleText_ = "";
  if (w->autoConfirm_) {
    w->doConfirm();
    return;
  }
  w->draw(); // content + clear record status icon
}

void SttInputWindow::onSttError(void *ctx, const char * /*reason*/) {
  SttInputWindow *w = static_cast<SttInputWindow *>(ctx);
  w->draw();
}

void SttInputWindow::onSttDraw(void *ctx) {
  static_cast<SttInputWindow *>(ctx)->draw();
}

// ---- init / enter / exit ----

void SttInputWindow::init(const char *title, const char *icon,
                          SttConfirmCb onConfirm, SttCancelCb onCancel,
                          void *ctx, const SttInputOpts &opts) {
  title_ = title ? title : "Input";
  icon_ = icon ? icon : "chat";
  confirmCb_ = onConfirm;
  cancelCb_ = onCancel;
  ctx_ = ctx;
  autoConfirm_ = opts.autoConfirm;
  confirmLabel_ = opts.confirmLabel;
  rerecordLabel_ = opts.rerecordLabel;
  cancelLabel_ = opts.cancelLabel;
}

const char *SttInputWindow::statusIcon() const {
  SttState st = gSttSession.state();
  if (st == SttState::Connecting || st == SttState::Listening) {
    return "record";
  }
  return nullptr;
}

uint16_t SttInputWindow::statusIconColor() const { return ILI9341_RED; }

void SttInputWindow::setupMenu() {
  confirmItems_[0] = {confirmLabel_ ? confirmLabel_ : "Confirm", 0, "check",
                      false, nullptr};
  confirmItems_[1] = {rerecordLabel_ ? rerecordLabel_ : "Re-record", 1, "chat",
                      false, nullptr};
  confirmItems_[2] = {cancelLabel_ ? cancelLabel_ : "Cancel", 2, "back", false,
                      nullptr};
  confirmMenu_.setItems(confirmItems_, 3);
  confirmMenu_.setWrapNavigation(true);
  confirmMenu_.setPadX(kUiPadX);
  confirmMenu_.setClip(contentTop(), tft.height());
  confirmMenu_.resetFocus();
}

void SttInputWindow::onEnter() {
  Window::onEnter();
  transcript_ = "";
  lastBubbleText_ = "";
  setupMenu();
  startListening();
}

void SttInputWindow::onExit() {
  SttState st = gSttSession.state();
  if (st == SttState::Listening || st == SttState::Connecting) {
    gSttSession.abort();
  }
}

void SttInputWindow::startListening() {
  transcript_ = "";
  lastBubbleText_ = "";
  setupMenu();
  gSttSession.start(onSttDone, onSttError, this, onSttDraw);
}

void SttInputWindow::doConfirm() {
  gWindows.pop();
  if (confirmCb_) {
    confirmCb_(ctx_, transcript_.c_str());
  }
}

void SttInputWindow::doCancel() {
  gSttSession.abort();
  gWindows.pop();
  if (cancelCb_) {
    cancelCb_(ctx_);
  }
}

// ---- tick ----

void SttInputWindow::onTick() {
  gSttSession.tick();
  SttState st = gSttSession.state();
  if (st != SttState::Listening) {
    return;
  }
  uint32_t now = millis();
  if (now - lastUiMs_ > 200) {
    lastUiMs_ = now;
    updateBubbleIfNeeded();
  }
}

// ---- events ----

bool SttInputWindow::onEvent(JoyEvent e) {
  SttState st = gSttSession.state();

  if (st == SttState::Listening || st == SttState::Connecting) {
    if (e == JoyEvent::Ok) {
      gSttSession.stop();
      return true;
    }
    if (e == JoyEvent::Back) {
      doCancel();
      return true;
    }
    return true;
  }

  if (st == SttState::Done) {
    if (e == JoyEvent::Ok) {
      int f = confirmMenu_.focusedIndex();
      if (f == 0) {
        doConfirm();
      } else if (f == 1) {
        startListening();
      } else {
        doCancel();
      }
      return true;
    }
    if (e == JoyEvent::Back) {
      doCancel();
      return true;
    }
    bool moved = false;
    if (confirmMenu_.onEvent(e, &moved)) {
      if (moved) {
        paintMenu();
      }
      return true;
    }
    return true;
  }

  if (st == SttState::Error || st == SttState::Idle) {
    if (e == JoyEvent::Ok) {
      startListening();
      return true;
    }
    if (e == JoyEvent::Back) {
      doCancel();
      return true;
    }
  }
  return false;
}

// ---- drawing helpers ----

String SttInputWindow::currentText() const {
  SttState st = gSttSession.state();
  if (st == SttState::Listening || st == SttState::Connecting) {
    String t = deepgramCopyListeningText();
    return t.length() ? (t + "...") : "...";
  }
  if (transcript_.length()) {
    return transcript_;
  }
  return deepgramFinalText().length() ? deepgramFinalText() : String("(empty)");
}

int16_t SttInputWindow::measureBubbleH(const char *text, int16_t innerW) const {
  TextStyle st;
  st.size = kUiBodySize;
  st.flags = TextFlagWrap;
  TextMetrics m = Text::measure(text, 0, 0, innerW, st);
  int h = m.height + 2 * kBubblePad;
  return (int16_t)(h < 40 ? 40 : h);
}

void SttInputWindow::drawBubble(int16_t x, int16_t y, int16_t w, int16_t h,
                                const char *text) {
  reclaimDisplay();
  int16_t y0, y1;
  if (!uiClipSpan(y, h, &y0, &y1)) {
    return;
  }
  if (y >= contentTop() && y + h <= tft.height()) {
    tft.fillRoundRect(x, y, w, h, kBubbleR, kBubbleBg);
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

void SttInputWindow::updateBubbleIfNeeded() {
  String text = currentText();
  if (text == lastBubbleText_) {
    return;
  }
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  const int16_t innerW = (int16_t)(maxW - 2 * kBubblePad);
  int16_t newH = measureBubbleH(text.c_str(), innerW);
  const int maxH = tft.height() - bubbleY_ - 8;
  if (newH > maxH) newH = (int16_t)maxH;

  reclaimDisplay();
  uiClipSet((int16_t)contentTop(), (int16_t)tft.height());
  int clearH = newH > bubbleH_ ? newH : bubbleH_;
  if (bubbleY_ + clearH > tft.height()) clearH = tft.height() - bubbleY_;
  if (clearH > 0) tft.fillRect(kUiPadX, bubbleY_, maxW, clearH, ILI9341_BLACK);
  bubbleH_ = newH;
  drawBubble(kUiPadX, bubbleY_, maxW, bubbleH_, text.c_str());
  lastBubbleText_ = text;
  uiClipClear();
}

void SttInputWindow::paintMenu() {
  confirmMenu_.setClip(contentTop(), tft.height());
  confirmMenu_.draw(menuY_);
}

void SttInputWindow::paintBubbleAndMenu(int ox, int oy) {
  const int16_t maxW  = (int16_t)(tft.width() - 2 * kUiPadX);
  const int16_t bubX  = (int16_t)(ox + kUiPadX);
  bubbleY_ = (int16_t)(oy + 12);

  String text = currentText();
  const int16_t innerW = (int16_t)(maxW - 2 * kBubblePad);
  bubbleH_ = measureBubbleH(text.c_str(), innerW);

  const int menuBlock = confirmMenu_.contentHeight() + 8;
  const int maxBubble = tft.height() - bubbleY_ - menuBlock;
  if (bubbleH_ > maxBubble && maxBubble > 40) {
    bubbleH_ = (int16_t)maxBubble;
  }
  drawBubble(bubX, bubbleY_, maxW, bubbleH_, text.c_str());
  lastBubbleText_ = text;

  menuY_ = (int16_t)(bubbleY_ + bubbleH_ + 12);
  paintMenu();
}

void SttInputWindow::paintConnecting(int ox, int oy) {
  TextStyle st;
  st.size  = kUiBodySize;
  st.color = ILI9341_YELLOW;
  st.flags = TextFlagWrap;
  Text::draw("Connecting...", (int16_t)(ox + kUiPadX), (int16_t)(oy + 12),
             (int16_t)(tft.width() - 2 * kUiPadX), st);
}

void SttInputWindow::paintListening(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  TextStyle st;
  st.size  = kUiBodySize;
  st.color = ILI9341_CYAN;
  st.flags = TextFlagNoWrap | TextFlagTruncate;
  st.maxLines = 1;
  TextMetrics m = Text::draw("Listening...", (int16_t)(ox + kUiPadX),
                              (int16_t)(oy + 10), maxW, st);
  bubbleY_ = (int16_t)(m.nextY + 10);
  const int16_t innerW = (int16_t)(maxW - 2 * kBubblePad);
  String text = currentText();
  bubbleH_ = measureBubbleH(text.c_str(), innerW);
  const int maxH = tft.height() - bubbleY_ - 8;
  if (bubbleH_ > maxH) bubbleH_ = (int16_t)maxH;
  drawBubble((int16_t)(ox + kUiPadX), bubbleY_, maxW, bubbleH_, text.c_str());
  lastBubbleText_ = text;
}

void SttInputWindow::paintError(int ox, int oy) {
  TextStyle st;
  st.size  = kUiBodySize;
  st.color = ILI9341_RED;
  st.flags = TextFlagWrap;
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  TextMetrics m = Text::draw("STT error", (int16_t)(ox + kUiPadX),
                              (int16_t)(oy + 12), maxW, st);
  st.color = ILI9341_DARKGREY;
  st.size  = kUiHintSize;
  Text::draw("ok = retry   back = cancel", (int16_t)(ox + kUiPadX),
             (int16_t)(m.nextY + 12), maxW, st);
}

void SttInputWindow::drawContent(int ox, int oy) {
  switch (gSttSession.state()) {
  case SttState::Connecting:
    paintConnecting(ox, oy);
    break;
  case SttState::Listening:
    paintListening(ox, oy);
    break;
  case SttState::Done:
    paintBubbleAndMenu(ox, oy);
    break;
  case SttState::Error:
    paintError(ox, oy);
    break;
  default:
    break;
  }
}

// ---- factory ----

Window *windowSttInput(const char *title, const char *icon,
                       SttConfirmCb onConfirm, SttCancelCb onCancel, void *ctx,
                       const SttInputOpts &opts) {
  sSttInput.init(title, icon, onConfirm, onCancel, ctx, opts);
  return &sSttInput;
}
