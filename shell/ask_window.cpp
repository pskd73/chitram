#include "ask_window.h"

#include "config.h"
#include "deepgram.h"
#include "display.h"
#include "input.h"
#include "menu.h"
#include "net_services.h"
#include "photo_window.h"
#include "settings.h"
#include "stt_session.h"
#include "status_window.h"
#include "ui_clip.h"
#include "ui_text.h"
#include "icon.h"

#include <Adafruit_ILI9341.h>
#include <string.h>

static MenuItem kDoneMenuItems[] = {
    {"Generate", 0, "image", false, nullptr},
    {"Ask again", 1, "chat", false, nullptr},
    {"Home", 2, "home", false, nullptr},
};
static const int kDoneMenuN = 3;
static const uint16_t kBubbleBg = 0xE71C;   // light gray (iMessage-ish)
static const uint16_t kBubbleFg = 0x18C3;   // dark text
static const uint16_t kBubblePad = 10;
static const int kBubbleRadius = 12;

class AskWindow : public Window {
public:
  const char *title() const override {
    return sourcePath_[0] ? "Edit" : "Ask";
  }
  const char *icon() const override {
    return sourcePath_[0] ? "image" : "chat";
  }
  void setSourcePath(const char *path);
  void onEnter() override;
  void onExit() override;
  void onTick() override;
  bool onEvent(JoyEvent e) override;
  void drawContent(int originX, int originY) override;

private:
  String lastBubbleText_;
  String transcript_; // snapshot after STT Done
  char sourcePath_[48] = {};
  int16_t labelY_ = 0;
  int16_t bubbleY_ = 0;
  int16_t bubbleH_ = 0;
  int16_t menuY_ = 0;
  uint32_t lastUiMs_ = 0;
  Menu doneMenu_;

  // SttSession callbacks (static, bound to AskWindow instance via ctx).
  static void onSttDone(void *ctx, const String &text);
  static void onSttError(void *ctx, const char *reason);
  static void onSttDraw(void *ctx);

  void startListening();
  void stopListening();
  void doGenerate();
  void goHome();

  String bubbleDisplayText() const;
  int16_t measureBubbleHeight(const char *text, int16_t innerW) const;
  void drawBubble(int16_t x, int16_t y, int16_t w, int16_t h, const char *text);
  void paintListeningLayout(int ox, int oy);
  void paintDoneLayout(int ox, int oy);
  void paintErrorLayout(int ox, int oy);
  void paintConnecting(int ox, int oy);
  void updateBubbleIfNeeded();
  void setupDoneMenu();
  void paintDoneMenu();
};

static AskWindow sAsk;

// ---- SttSession callback stubs ----

void AskWindow::onSttDone(void *ctx, const String &text) {
  AskWindow *w = static_cast<AskWindow *>(ctx);
  w->transcript_ = text;
  w->lastBubbleText_ = "";
  w->drawContentArea();
  inputLog("ask: done \"%s\"", text.c_str());
}

void AskWindow::onSttError(void *ctx, const char *reason) {
  AskWindow *w = static_cast<AskWindow *>(ctx);
  w->drawContentArea();
  inputLog("ask: error %s", reason ? reason : "");
}

void AskWindow::onSttDraw(void *ctx) {
  AskWindow *w = static_cast<AskWindow *>(ctx);
  w->draw();
}

// ---- helpers ----

void AskWindow::setupDoneMenu() {
  doneMenu_.setItems(kDoneMenuItems, kDoneMenuN);
  doneMenu_.setWrapNavigation(true);
  doneMenu_.setPadX(kUiPadX);
  doneMenu_.setClip(kWinTitleH, tft.height());
  doneMenu_.resetFocus();
}

void AskWindow::paintDoneMenu() {
  doneMenu_.setClip(contentTop(), tft.height());
  doneMenu_.draw(menuY_);
}

void AskWindow::setSourcePath(const char *path) {
  if (!path || !path[0]) {
    sourcePath_[0] = '\0';
    return;
  }
  strncpy(sourcePath_, path, sizeof(sourcePath_) - 1);
  sourcePath_[sizeof(sourcePath_) - 1] = '\0';
}

String AskWindow::bubbleDisplayText() const {
  SttState st = gSttSession.state();
  String t;
  if (st == SttState::Listening || st == SttState::Connecting) {
    t = deepgramCopyListeningText();
  } else {
    t = transcript_.length() ? transcript_ : deepgramFinalText();
  }

  if (st == SttState::Listening) {
    t = t.length() ? (t + "...") : "...";
  } else if (!t.length()) {
    t = "(empty)";
  }
  return t;
}

int16_t AskWindow::measureBubbleHeight(const char *text,
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

void AskWindow::drawBubble(int16_t x, int16_t y, int16_t w, int16_t h,
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

// ---- lifecycle ----

void AskWindow::onEnter() {
  Window::onEnter();
  lastBubbleText_ = "";
  setupDoneMenu();
  startListening();
}

void AskWindow::onExit() {
  SttState st = gSttSession.state();
  if (st == SttState::Listening || st == SttState::Connecting) {
    gSttSession.abort();
  }
}

void AskWindow::startListening() {
  transcript_ = "";
  lastBubbleText_ = "";
  lastUiMs_ = 0;
  setupDoneMenu();
  gSttSession.start(onSttDone, onSttError, this, onSttDraw);
}

void AskWindow::stopListening() {
  gSttSession.stop();
}

void AskWindow::goHome() {
  gSttSession.abort();
  gWindows.pop();
}

void AskWindow::doGenerate() {
  String prompt = transcript_;
  if (!prompt.length()) {
    prompt = deepgramFinalText();
  }
  if (!prompt.length()) {
    drawContentArea();
    return;
  }
  inputLog("ask: generate image...");
  gWindows.replaceTop(windowGenerating());
  delay(30);

  char path[48] = {};
  const char *ref = sourcePath_[0] ? sourcePath_ : nullptr;
  char savedSource[48];
  strncpy(savedSource, sourcePath_, sizeof(savedSource) - 1);
  savedSource[sizeof(savedSource) - 1] = '\0';

  bool ok = generateAndShowImage(prompt, path, sizeof(path), false, ref);
  if (!ok || !path[0]) {
    if (savedSource[0]) {
      gWindows.replaceTop(windowAskModify(savedSource));
    } else {
      gWindows.replaceTop(windowAsk());
    }
    gSttSession.abort();
    drawContentArea();
    return;
  }

  gWindows.replaceTop(windowImagePreview(path));
}

// ---- paint ----

void AskWindow::paintConnecting(int ox, int oy) {
  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_YELLOW;
  st.flags = TextFlagWrap;
  Text::draw("Connecting...", (int16_t)(ox + kUiPadX), (int16_t)(oy + 12),
             (int16_t)(tft.width() - 2 * kUiPadX), st);
}

void AskWindow::paintErrorLayout(int ox, int oy) {
  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_RED;
  st.flags = TextFlagWrap;
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  // Retrieve last error from session (stored before errorCb fires)
  TextMetrics m = Text::draw("STT error",
                              (int16_t)(ox + kUiPadX), (int16_t)(oy + 12),
                              maxW, st);
  st.color = ILI9341_DARKGREY;
  st.size = kUiHintSize;
  Text::draw("ok = retry   back = home", (int16_t)(ox + kUiPadX),
             (int16_t)(m.nextY + 12), maxW, st);
}

void AskWindow::paintListeningLayout(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  labelY_ = (int16_t)(oy + 10);

  TextStyle st;
  st.size = kUiBodySize;
  st.color = ILI9341_CYAN;
  st.flags = TextFlagNoWrap | TextFlagTruncate;
  st.maxLines = 1;
  TextMetrics m = Text::draw(
#if defined(CHITRAM_AUDIO_ONLY)
      "Recording to SD...",
#else
      "Listening...",
#endif
      (int16_t)(ox + kUiPadX), labelY_, maxW, st);

  bubbleY_ = (int16_t)(m.nextY + 10);
  const int16_t bubbleW = maxW;
  const int16_t bubbleX = (int16_t)(ox + kUiPadX);
  const int16_t innerW = (int16_t)(bubbleW - 2 * kBubblePad);
  String text = bubbleDisplayText();
  bubbleH_ = measureBubbleHeight(text.c_str(), innerW);
  const int maxH = tft.height() - bubbleY_ - 8;
  if (bubbleH_ > maxH) {
    bubbleH_ = (int16_t)maxH;
  }
  drawBubble(bubbleX, bubbleY_, bubbleW, bubbleH_, text.c_str());
  lastBubbleText_ = text;
}

void AskWindow::paintDoneLayout(int ox, int oy) {
  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  const int16_t bubbleX = (int16_t)(ox + kUiPadX);
  bubbleY_ = (int16_t)(oy + 12);

  String text = bubbleDisplayText();
  const int16_t innerW = (int16_t)(maxW - 2 * kBubblePad);
  bubbleH_ = measureBubbleHeight(text.c_str(), innerW);

  const int menuBlock = doneMenu_.contentHeight() + 8;
  const int maxBubble = tft.height() - bubbleY_ - menuBlock;
  if (bubbleH_ > maxBubble && maxBubble > 40) {
    bubbleH_ = (int16_t)maxBubble;
  }

  drawBubble(bubbleX, bubbleY_, maxW, bubbleH_, text.c_str());
  lastBubbleText_ = text;

  menuY_ = (int16_t)(bubbleY_ + bubbleH_ + 12);
  paintDoneMenu();
}

void AskWindow::updateBubbleIfNeeded() {
  String text = bubbleDisplayText();
  if (text == lastBubbleText_) {
    return;
  }

  const int16_t maxW = (int16_t)(tft.width() - 2 * kUiPadX);
  const int16_t bubbleX = kUiPadX;
  const int16_t innerW = (int16_t)(maxW - 2 * kBubblePad);
  int16_t newH = measureBubbleHeight(text.c_str(), innerW);
  const int maxH = tft.height() - bubbleY_ - 8;
  if (newH > maxH) {
    newH = (int16_t)maxH;
  }

  reclaimDisplay();
  uiClipSet((int16_t)contentTop(), (int16_t)tft.height());
  int clearH = newH > bubbleH_ ? newH : bubbleH_;
  if (bubbleY_ + clearH > tft.height()) {
    clearH = tft.height() - bubbleY_;
  }
  if (clearH > 0) {
    tft.fillRect(bubbleX, bubbleY_, maxW, clearH, ILI9341_BLACK);
  }
  bubbleH_ = newH;
  drawBubble(bubbleX, bubbleY_, maxW, bubbleH_, text.c_str());
  lastBubbleText_ = text;
  uiClipClear();
}

// ---- onTick ----

void AskWindow::onTick() {
  gSttSession.tick();

  SttState st = gSttSession.state();
  if (st != SttState::Listening) {
    return;
  }

  uint32_t now = millis();
  if (now - lastUiMs_ > 200) {
    lastUiMs_ = now;
#if !defined(CHITRAM_AUDIO_ONLY)
    updateBubbleIfNeeded();
#endif
  }
}

// ---- onEvent ----

bool AskWindow::onEvent(JoyEvent e) {
  SttState st = gSttSession.state();

  if (st == SttState::Listening) {
    if (e == JoyEvent::Ok) {
      stopListening();
      return true;
    }
    if (e == JoyEvent::Back) {
      inputLog("ask: back — restart");
      gSttSession.abort();
      startListening();
      return true;
    }
    return true;
  }

  if (st == SttState::Done) {
    if (e == JoyEvent::Ok) {
      const int focus = doneMenu_.focusedIndex();
      if (focus == 0) {
        doGenerate();
      } else if (focus == 1) {
        startListening();
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

  if (st == SttState::Error || st == SttState::Idle) {
    if (e == JoyEvent::Ok) {
      startListening();
      return true;
    }
    if (e == JoyEvent::Back) {
      return false;
    }
  }

  if (e == JoyEvent::Back) {
    return false;
  }
  return false;
}

// ---- drawContent ----

void AskWindow::drawContent(int ox, int oy) {
  switch (gSttSession.state()) {
  case SttState::Connecting:
    paintConnecting(ox, oy);
    break;
  case SttState::Listening:
    paintListeningLayout(ox, oy);
    break;
  case SttState::Done:
    paintDoneLayout(ox, oy);
    break;
  case SttState::Error:
    paintErrorLayout(ox, oy);
    break;
  default:
    break;
  }
}

Window *windowAsk() {
  sAsk.setSourcePath(nullptr);
  return &sAsk;
}

Window *windowAskModify(const char *path) {
  sAsk.setSourcePath(path);
  return &sAsk;
}
