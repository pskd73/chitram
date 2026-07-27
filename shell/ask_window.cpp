#include "ask_window.h"

#include "audio_rec.h"
#include "config.h"
#include "deepgram.h"
#include "display.h"
#include "input.h"
#include "menu.h"
#include "mic.h"
#include "net_services.h"
#include "settings.h"
#include "status_window.h"
#include "ui_clip.h"
#include "ui_text.h"
#include "icon.h"

#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <string.h>

enum class AskState : uint8_t {
  Idle,
  Connecting,
  Listening,
  Done,
  Error,
};

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
  AskState state_ = AskState::Idle;
  const char *error_ = nullptr;
  uint32_t listenStartedMs_ = 0;
  uint32_t lastUiMs_ = 0;
  uint32_t lastSoundMs_ = 0;
  bool heardSound_ = false;
  String lastBubbleText_;
  String lastActivityText_; // last interim/final seen (activity for silence)
  String transcript_; // kept after STT closes / image gen clears deepgram
  char sourcePath_[48] = {};
  int16_t labelY_ = 0;
  int16_t bubbleY_ = 0;
  int16_t bubbleH_ = 0;
  int16_t menuY_ = 0;
  Menu doneMenu_;

  void startListening();
  void stopListening();
  void abortListening();
  void finishListeningCapture();
  void failListening(const char *err);
  void doGenerate();
  void goHome();

  String combinedTranscript() const;
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

String AskWindow::combinedTranscript() const {
  // Deepgram emits multiple is_final segments per utterance — use the
  // accumulated finalText, plus any live interim hypothesis.
  if (state_ == AskState::Listening || state_ == AskState::Connecting) {
    return deepgramCopyListeningText();
  }
  if (transcript_.length()) {
    return transcript_;
  }
  return deepgramFinalText();
}

String AskWindow::bubbleDisplayText() const {
  String t = combinedTranscript();
  if (state_ == AskState::Listening) {
    if (t.length()) {
      t += "...";
    } else {
      t = "...";
    }
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
  // Clip to viewport
  int16_t y0, y1;
  if (!uiClipSpan(y, h, &y0, &y1)) {
    return;
  }
  // Full bubble when fully visible; otherwise fill clipped
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

void AskWindow::onEnter() {
  Window::onEnter();
  error_ = nullptr;
  lastBubbleText_ = "";
  setupDoneMenu();
  startListening();
}

void AskWindow::onExit() {
  if (state_ == AskState::Listening || state_ == AskState::Connecting) {
    abortListening();
  }
}

void AskWindow::startListening() {
  state_ = AskState::Connecting;
  error_ = nullptr;
  lastBubbleText_ = "";
  transcript_ = "";
  setupDoneMenu();
  deepgramClearText();
  displayResetTranscriptCache();
  draw();

#if !defined(CHITRAM_AUDIO_ONLY)
  if (WiFi.status() != WL_CONNECTED) {
    inputLog("ask: wifi...");
    gWindows.push(windowWifiConnecting());
    bool ok = connectWifi();
    gWindows.pop(); // back to Ask
    if (!ok) {
      state_ = AskState::Error;
      error_ = "WiFi failed";
      draw();
      return;
    }
  }
#endif

  // Probe mic channel only (no capture tasks yet — save RAM for TLS).
  inputLog("ask: mic probe...");
  if (!chooseMicChannel()) {
    state_ = AskState::Error;
    error_ = "Mic init failed";
    draw();
    return;
  }

#if defined(CHITRAM_AUDIO_ONLY)
  inputLog("ask: AUDIO ONLY — ring → SD (no Deepgram)");
#else
  // Connect Deepgram BEFORE starting capture (needs internal heap for TLS).
  inputLog("ask: deepgram...");
  Serial.printf("pre-dg heap=%u maxAlloc=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  if (!connectDeepgram()) {
    stopI2S();
    state_ = AskState::Error;
    error_ = "STT connect failed";
    draw();
    return;
  }
  Serial.printf("post-dg heap=%u\n", (unsigned)ESP.getFreeHeap());
#endif

  if (settingsSaveAudio()) {
    if (!audioRecBegin()) {
#if defined(CHITRAM_AUDIO_ONLY)
      stopI2S();
      state_ = AskState::Error;
      error_ = "SD record failed";
      draw();
      return;
#else
      inputLog("ask: audio rec failed (continuing without SD)");
#endif
    }
  } else {
    inputLog("ask: SD audio save off");
  }

  micResetSession();
  micSetDiscardSamples(SAMPLE_RATE / 4);
  resetAudioDsp();
  micSetStreamFlush(flushPcmToDeepgram);
  micStartCapture(); // also starts egress

  state_ = AskState::Listening;
  listenStartedMs_ = millis();
  lastUiMs_ = 0;
  lastSoundMs_ = millis();
  heardSound_ = false;
  lastBubbleText_ = "";
  lastActivityText_ = "";
#if defined(CHITRAM_AUDIO_ONLY)
  inputLog("ask: recording — press ok to stop");
#else
  inputLog("ask: listening — ok to stop");
#endif
  draw();
}

void AskWindow::abortListening() {
  finishListeningCapture();
#if !defined(CHITRAM_AUDIO_ONLY)
  closeDeepgram();
#endif
  state_ = AskState::Idle;
}

void AskWindow::finishListeningCapture() {
  // Stops capture + drains egress (Deepgram/SD buffer), then tears down I2S.
  micFlushPending(flushPcmToDeepgram);
  if (audioRecActive()) {
    char path[40];
    audioRecEnd(path, sizeof(path));
    if (path[0]) {
      inputLog("ask: saved %s", path);
    }
  }
  stopI2S();
}

void AskWindow::failListening(const char *err) {
  finishListeningCapture();
#if !defined(CHITRAM_AUDIO_ONLY)
  closeDeepgram();
#endif
  state_ = AskState::Error;
  error_ = err ? err : "Error";
  drawContentArea();
}

void AskWindow::stopListening() {
  if (state_ != AskState::Listening) {
    return;
  }
  inputLog("ask: stop");

  state_ = AskState::Done;
  doneMenu_.resetFocus();
  lastBubbleText_ = "";

  finishListeningCapture();

#if defined(CHITRAM_AUDIO_ONLY)
  transcript_ = "Saved to /audio (see serial)";
  drawContentArea();
  inputLog("ask: audio-only done");
  return;
#else
  if (deepgramInterimText().length()) {
    if (deepgramFinalText().length()) {
      deepgramFinalText() += ' ';
    }
    deepgramFinalText() += deepgramInterimText();
    deepgramLastFinalText() = deepgramInterimText();
    deepgramInterimText() = "";
  }
  transcript_ = deepgramFinalText();

  drawContentArea();
  String shown = transcript_;

  deepgramFinalizeAndClose(400, 200);

  if (deepgramInterimText().length()) {
    if (deepgramFinalText().length()) {
      deepgramFinalText() += ' ';
    }
    deepgramFinalText() += deepgramInterimText();
    deepgramLastFinalText() = deepgramInterimText();
    deepgramInterimText() = "";
  }
  transcript_ = deepgramFinalText();
  if (transcript_ != shown) {
    lastBubbleText_ = "";
    drawContentArea();
  }

  inputLog("ask: done len=%u text=\"%s\"", (unsigned)transcript_.length(),
           transcript_.c_str());
#endif
}

void AskWindow::goHome() {
  abortListening();
  // Pop back to home (single level from Home → Ask)
  gWindows.pop();
}

void AskWindow::doGenerate() {
  String prompt = transcript_;
  if (!prompt.length()) {
    prompt = deepgramFinalText();
  }
  if (!prompt.length()) {
    error_ = "Nothing to ask";
    state_ = AskState::Error;
    drawContentArea();
    return;
  }
  inputLog("ask: generate image...");
  // Replace Ask with Generating (don't grow the stack)
  gWindows.replaceTop(windowGenerating());
  delay(30);

  char path[48] = {};
  const char *ref = sourcePath_[0] ? sourcePath_ : nullptr;
  char savedSource[48];
  strncpy(savedSource, sourcePath_, sizeof(savedSource) - 1);
  savedSource[sizeof(savedSource) - 1] = '\0';

  bool ok = generateAndShowImage(prompt, path, sizeof(path), false, ref);
  if (!ok || !path[0]) {
    // Restore Ask (edit or plain); onEnter starts listen — stop and show error
    if (savedSource[0]) {
      gWindows.replaceTop(windowAskModify(savedSource));
    } else {
      gWindows.replaceTop(windowAsk());
    }
    abortListening();
    state_ = AskState::Error;
    error_ = "Generate failed";
    draw();
    return;
  }

  gWindows.replaceTop(windowImagePreview(path));
}

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
  TextMetrics m = Text::draw(error_ ? error_ : "Error",
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
  // Cap bubble so it fits viewport
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
  // Clear old bubble region (use larger of old/new height)
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

void AskWindow::onTick() {
  if (state_ != AskState::Listening) {
    return;
  }

#if !defined(CHITRAM_AUDIO_ONLY)
  deepgramPoll();

  if (!deepgramConnected() || !deepgramSocketAvailable()) {
    inputLog("ask: STT dropped");
    failListening("STT disconnected");
    return;
  }
#endif

  if (!pollMicAndStream()) {
    inputLog("ask: mic/stream error");
    failListening("Mic/stream error");
    return;
  }

  uint32_t now = millis();

  int16_t peak = micTakeLivePeak();
#if defined(CHITRAM_AUDIO_ONLY)
  static uint32_t lastDiagMs = 0;
  if (now - lastDiagMs >= 500) {
    lastDiagMs = now;
    inputLog("rec peak=%d pending=%u bytes=%lu g=%.1f", (int)peak,
             (unsigned)micPendingSamples(),
             (unsigned long)audioRecBytes(), micDspGain());
  }
  const bool loudPeak = peak >= SILENCE_SOUND_PEAK;
  if (loudPeak) {
    lastSoundMs_ = now;
    heardSound_ = true;
  }
#else
  String activity = deepgramCopyListeningText();
  const bool sttGrew =
      activity.length() && activity != lastActivityText_;
  const bool loudPeak = peak >= SILENCE_SOUND_PEAK;
  if (sttGrew || loudPeak) {
    lastSoundMs_ = now;
    heardSound_ = true;
    if (sttGrew) {
      lastActivityText_ = activity;
    }
  }

  if (deepgramTakeUtteranceEnd() &&
      (activity.length() || lastActivityText_.length())) {
    inputLog("ask: utterance end — stop");
    stopListening();
    return;
  }
#endif

  if (heardSound_ && (now - lastSoundMs_) >= SILENCE_STOP_MS) {
#if !defined(CHITRAM_AUDIO_ONLY)
    inputLog("ask: silence %lums — stop", (unsigned long)(now - lastSoundMs_));
    stopListening();
    return;
#endif
  }

  if (now - lastUiMs_ > 200) {
    lastUiMs_ = now;
#if !defined(CHITRAM_AUDIO_ONLY)
    updateBubbleIfNeeded();
#endif
  }

  if (now - listenStartedMs_ >= MAX_LISTEN_MS) {
    inputLog("ask: max time");
    stopListening();
  }
}

bool AskWindow::onEvent(JoyEvent e) {
  if (state_ == AskState::Listening) {
    if (e == JoyEvent::Ok) {
      stopListening();
      return true;
    }
    if (e == JoyEvent::Back) {
      // Restart session: drop current text and listen again
      inputLog("ask: back — restart listen");
      abortListening();
      startListening();
      return true;
    }
    return true; // swallow other keys while listening
  }

  if (state_ == AskState::Done) {
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
      return true; // already popped
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

  if (state_ == AskState::Error || state_ == AskState::Idle) {
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

void AskWindow::drawContent(int ox, int oy) {
  switch (state_) {
  case AskState::Connecting:
    paintConnecting(ox, oy);
    break;
  case AskState::Listening:
    paintListeningLayout(ox, oy);
    break;
  case AskState::Done:
    paintDoneLayout(ox, oy);
    break;
  case AskState::Error:
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
