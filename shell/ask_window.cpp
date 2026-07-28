#include "ask_window.h"

#include "config.h"
#include "display.h"
#include "input.h"
#include "net_services.h"
#include "profiles.h"
#include "stt_input_window.h"
#include "ui_clip.h"
#include "ui_text.h"
#include "window.h"

#include <Adafruit_ILI9341.h>
#include <esp_heap_caps.h>
#include <string.h>

static const char *kAskSystem =
    "You are a helpful voice assistant on a small handheld device. "
    "Answer clearly in plain text. No markdown, no bullet lists. "
    "Be concise for simple questions. When the user asks for a long "
    "story or detailed answer, write the full response without cutting off.";

static const uint8_t kAskBodySize = 2;
static const uint8_t kAskLabelSize = 1;
static const uint16_t kUserLabel = ILI9341_CYAN;
static const uint16_t kAiLabel = ILI9341_YELLOW;
static const uint16_t kUserText = ILI9341_WHITE;
static const uint16_t kAiText = 0xC618; // light gray
static const int kMaxMsgs = 6; // 3 turns — keeps scroll/RAM bounded
static const size_t kMaxUserChars = 2000;
static const size_t kMaxAiChars = 48000;

enum class AskPhase : uint8_t {
  Idle,
  Thinking,
  Error,
};

struct AskMsg {
  bool isUser = false;
  char *text = nullptr; // PSRAM (fallback DRAM)
  int height = -1;      // cached wrapped height at current maxW
};

class AskWindow : public Window {
public:
  const char *title() const override {
    const Profile *p = profilesActive();
    return (p && p->name[0]) ? p->name : "Ask";
  }
  const char *icon() const override { return "chat"; }
  const char *statusIcon() const override;
  uint16_t statusIconColor() const override;

  void onEnter() override;
  void onExit() override;
  void onFocus() override;
  void onTick() override;
  bool onEvent(JoyEvent e) override;
  int scrollContentHeight() const override { return contentH_; }
  void drawContent(int originX, int originY) override;

private:
  AskPhase phase_ = AskPhase::Idle;
  AskMsg msgs_[kMaxMsgs];
  int msgCount_ = 0;
  String errorMsg_;
  // Live SSE preview (points at openRouterChat PSRAM buffer during call).
  const char *streamText_ = nullptr;
  size_t streamLen_ = 0;
  uint32_t lastStreamUiMs_ = 0;
  bool pendingReply_ = false;
  bool stickToReplyStart_ = true; // auto-pin viewport to start of AI reply
  bool autoListen_ = false;       // open listen once on first enter
  int contentH_ = 0;
  int16_t layoutMaxW_ = 0;

  static void onListenConfirm(void *ctx, const char *text);
  static void onListenCancel(void *ctx);
  static void onChatChunk(void *ctx, const char *textSoFar, size_t len);

  static char *allocText(size_t bytes);
  static void freeText(char *&p);

  void clearHistory();
  // Copies text into a new PSRAM buffer.
  void pushMessage(bool isUser, const char *text);
  void pushMessage(bool isUser, const String &text) {
    pushMessage(isUser, text.c_str());
  }
  // Takes ownership of owned (PSRAM/heap); must be freeable with free().
  void pushMessageOwned(bool isUser, char *owned, size_t len);
  void invalidateHeights();
  void goIdle();
  void openListen();
  void requestReply(const String &userText);
  void doReply();
  int drawMsg(int x, int y, int maxW, const char *text, uint16_t color);
  int measureMsgH(const char *text, int maxW) const;
  int estimateMsgH(const char *text, int maxW) const;
  int msgHeight(int index, int maxW);
  int contentYForMsg(int index, int maxW) const;
  void scrollToReplyStart();
  bool scrollAsk(JoyEvent e);
};

static AskWindow sAsk;

const char *AskWindow::statusIcon() const {
  if (phase_ == AskPhase::Thinking) {
    return "brain";
  }
  return nullptr;
}

uint16_t AskWindow::statusIconColor() const {
  if (phase_ == AskPhase::Thinking) {
    return ILI9341_YELLOW;
  }
  return ILI9341_WHITE;
}

char *AskWindow::allocText(size_t bytes) {
  char *p =
      (char *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) {
    p = (char *)malloc(bytes);
  }
  return p;
}

void AskWindow::freeText(char *&p) {
  if (p) {
    free(p);
    p = nullptr;
  }
}

void AskWindow::onListenConfirm(void *ctx, const char *text) {
  AskWindow *w = static_cast<AskWindow *>(ctx);
  String t = text ? text : "";
  t.trim();
  inputLog("ask: heard \"%s\"", t.c_str());
  if (!t.length() || t == "(empty)") {
    w->goIdle();
    return;
  }
  w->requestReply(t);
}

void AskWindow::onListenCancel(void *ctx) {
  AskWindow *w = static_cast<AskWindow *>(ctx);
  w->goIdle();
}

void AskWindow::onChatChunk(void *ctx, const char *textSoFar, size_t len) {
  AskWindow *w = static_cast<AskWindow *>(ctx);
  if (!w || !textSoFar) {
    return;
  }
  w->streamText_ = textSoFar;
  w->streamLen_ = len;
  uint32_t now = millis();
  if (now - w->lastStreamUiMs_ < 250 && len > 80) {
    return;
  }
  w->lastStreamUiMs_ = now;
  w->drawContentArea();
  if (w->stickToReplyStart_) {
    w->scrollToReplyStart();
  }
  Serial.printf("ask: ui len=%u contentH=%d scroll=%d maxScroll=%d\n",
                (unsigned)len, w->contentH_, w->scrollY(), w->maxScroll());
}

void AskWindow::clearHistory() {
  for (int i = 0; i < kMaxMsgs; ++i) {
    freeText(msgs_[i].text);
    msgs_[i].isUser = false;
    msgs_[i].height = -1;
  }
  msgCount_ = 0;
  layoutMaxW_ = 0;
}

void AskWindow::invalidateHeights() {
  for (int i = 0; i < msgCount_; ++i) {
    msgs_[i].height = -1;
  }
  layoutMaxW_ = 0;
}

void AskWindow::pushMessage(bool isUser, const char *text) {
  if (!text || !text[0]) {
    return;
  }
  while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') {
    ++text;
  }
  size_t n = strlen(text);
  while (n && (text[n - 1] == ' ' || text[n - 1] == '\n' || text[n - 1] == '\r' ||
               text[n - 1] == '\t')) {
    --n;
  }
  if (!n) {
    return;
  }
  const size_t cap = isUser ? kMaxUserChars : kMaxAiChars;
  if (n > cap) {
    n = cap;
  }

  char *buf = allocText(n + 1);
  if (!buf) {
    Serial.println("ask: msg alloc failed");
    return;
  }
  memcpy(buf, text, n);
  buf[n] = '\0';
  pushMessageOwned(isUser, buf, n);
}

void AskWindow::pushMessageOwned(bool isUser, char *owned, size_t len) {
  if (!owned) {
    return;
  }
  if (!len) {
    free(owned);
    return;
  }
  // Cap without realloc: just truncate in place.
  const size_t cap = isUser ? kMaxUserChars : kMaxAiChars;
  if (len > cap) {
    len = cap;
    owned[len] = '\0';
  }

  if (msgCount_ >= kMaxMsgs) {
    freeText(msgs_[0].text);
    for (int i = 0; i < kMaxMsgs - 1; ++i) {
      msgs_[i] = msgs_[i + 1];
    }
    msgs_[kMaxMsgs - 1].text = nullptr;
    msgs_[kMaxMsgs - 1].height = -1;
    --msgCount_;
  }

  const int maxW = tft.width() - 2 * kUiPadX;
  // Always estimate height for stored messages. Full Text::measure on 10k+
  // char replies is too slow and was collapsing contentH to one screen.
  const int h = estimateMsgH(owned, maxW > 0 ? maxW : 220);

  msgs_[msgCount_].isUser = isUser;
  msgs_[msgCount_].text = owned;
  msgs_[msgCount_].height = h;
  ++msgCount_;
  Serial.printf("ask: push %s len=%u height=%d msgs=%d\n",
                isUser ? "user" : "ai", (unsigned)len, h, msgCount_);
}

void AskWindow::goIdle() {
  pendingReply_ = false;
  phase_ = AskPhase::Idle;
  draw();
}

void AskWindow::openListen() {
  pendingReply_ = false;
  phase_ = AskPhase::Idle;
  gWindows.push(windowSttInput("Ask", "chat", onListenConfirm, onListenCancel,
                               this));
}

void AskWindow::requestReply(const String &userText) {
  pushMessage(true, userText);
  phase_ = AskPhase::Thinking;
  pendingReply_ = true;
  stickToReplyStart_ = true;
  streamText_ = nullptr;
  streamLen_ = 0;
  lastStreamUiMs_ = 0;
  draw(); // brain icon in title
  scrollToReplyStart();
}

void AskWindow::doReply() {
  pendingReply_ = false;

  const char *rolePtrs[kMaxMsgs];
  const char *textPtrs[kMaxMsgs];
  for (int i = 0; i < msgCount_; ++i) {
    rolePtrs[i] = msgs_[i].isUser ? "user" : "assistant";
    textPtrs[i] = msgs_[i].text ? msgs_[i].text : "";
  }

  streamText_ = nullptr;
  streamLen_ = 0;
  char *reply = nullptr;
  size_t replyLen = 0;
  const Profile *prof = profilesActive();
  const char *sys = kAskSystem;
  const char *model = CHAT_MODEL;
  if (prof && prof->type == ProfileType::Text) {
    if (prof->prompt[0]) {
      sys = prof->prompt;
    }
    if (prof->model[0]) {
      model = prof->model;
    }
  } else {
    const Profile *fb = profilesAtType(ProfileType::Text, 0);
    if (fb) {
      if (fb->prompt[0]) {
        sys = fb->prompt;
      }
      if (fb->model[0]) {
        model = fb->model;
      }
    }
  }
  bool ok = openRouterChat(sys, rolePtrs, textPtrs, msgCount_, &reply,
                           &replyLen, onChatChunk, this, model);
  streamText_ = nullptr;
  streamLen_ = 0;
  if (!ok || !reply || !replyLen) {
    if (reply) {
      free(reply);
    }
    phase_ = AskPhase::Error;
    errorMsg_ = "Chat failed";
    draw();
    return;
  }

  pushMessageOwned(false, reply, replyLen);
  reply = nullptr; // ownership transferred
  phase_ = AskPhase::Idle;
  stickToReplyStart_ = true;
  draw();
  scrollToReplyStart();
  Serial.printf("ask: done chars=%u contentH=%d maxScroll=%d scroll=%d\n",
                (unsigned)replyLen, contentH_, maxScroll(), scrollY());
}

int AskWindow::measureMsgH(const char *text, int maxW) const {
  if (!text || !text[0]) {
    return textLineH(kAskBodySize);
  }
  TextStyle st;
  st.size = kAskBodySize;
  st.flags = TextFlagWrap;
  TextMetrics m = Text::measure(text, 0, 0, maxW, st);
  return m.height > 0 ? m.height : textLineH(kAskBodySize);
}

// Fast height for live streaming — avoids full word-wrap on every chunk.
int AskWindow::estimateMsgH(const char *text, int maxW) const {
  if (!text || !text[0]) {
    return textLineH(kAskBodySize);
  }
  if (maxW < 1) {
    maxW = 220;
  }
  return Text::wrappedHeight(text, maxW, kAskBodySize);
}

int AskWindow::msgHeight(int index, int maxW) {
  if (index < 0 || index >= msgCount_) {
    return 0;
  }
  if (layoutMaxW_ != maxW) {
    invalidateHeights();
    layoutMaxW_ = (int16_t)maxW;
  }
  if (msgs_[index].height < 0) {
    msgs_[index].height = estimateMsgH(msgs_[index].text, maxW);
  }
  return msgs_[index].height;
}

int AskWindow::contentYForMsg(int index, int maxW) const {
  // Must match drawContent stacking (pad + messages).
  int y = 6;
  const int labelH = textLineH(kAskLabelSize);
  const int n = index < msgCount_ ? index : msgCount_;
  for (int i = 0; i < n; ++i) {
    int bh = msgs_[i].height;
    if (bh < 0) {
      bh = estimateMsgH(msgs_[i].text, maxW);
    }
    y += labelH + 2 + bh + 8;
  }
  return y;
}

int AskWindow::drawMsg(int x, int y, int maxW, const char *text,
                        uint16_t color) {
  if (!text || !text[0]) {
    return textLineH(kAskBodySize);
  }
  const int lh = textLineH(kAskBodySize);
  const int viewTop = contentTop();
  const int viewBot = tft.height();
  const int totalH = estimateMsgH(text, maxW);

  // One wrapped line per row at exact Y for continuous scroll.
  int first = 0;
  if (y < viewTop) {
    first = (viewTop - y + lh - 1) / lh;
    if (first < 0) {
      first = 0;
    }
  }
  int skipped = 0;
  const char *p =
      Text::skipWrappedLines(text, maxW, kAskBodySize, first, &skipped);
  int drawY = y + skipped * lh;

  TextStyle st;
  st.size = kAskBodySize;
  st.color = color;
  st.flags = TextFlagNoWrap | TextFlagTruncate;
  st.maxLines = 1;
  st.clipTop = (int16_t)viewTop;
  st.clipBottom = (int16_t)viewBot;

  char lineBuf[128];
  while (p && *p && drawY < viewBot) {
    int got = 0;
    const char *next =
        Text::skipWrappedLines(p, maxW, kAskBodySize, 1, &got);
    size_t n = 0;
    if (got > 0 && next && next > p) {
      n = (size_t)(next - p);
    } else {
      n = strlen(p);
      next = nullptr;
    }
    while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) {
      --n;
    }
    if (n >= sizeof(lineBuf)) {
      n = sizeof(lineBuf) - 1;
    }
    if (drawY + lh > viewTop && n > 0) {
      memcpy(lineBuf, p, n);
      lineBuf[n] = '\0';
      Text::draw(lineBuf, x, drawY, maxW, st);
    }
    if (got < 1) {
      break;
    }
    p = next;
    drawY += lh;
  }
  return totalH;
}

void AskWindow::scrollToReplyStart() {
  const int maxW = tft.width() - 2 * kUiPadX;
  int top;
  if (phase_ == AskPhase::Thinking && streamText_ && streamLen_ > 0) {
    top = contentYForMsg(msgCount_, maxW);
  } else if (msgCount_ > 0) {
    top = contentYForMsg(msgCount_ - 1, maxW);
  } else {
    top = 0;
  }
  const int before = scrollY();
  setScrollY(top);
  if (scrollY() != before) {
    drawContentArea();
  }
}

bool AskWindow::scrollAsk(JoyEvent e) {
  if (maxScroll() <= 0) {
    return false;
  }
  const int step = textLineH(kAskBodySize);
  if (e == JoyEvent::Up || e == JoyEvent::Left) {
    if (scrollY() <= 0) {
      return false;
    }
    scrollBy(-step);
    stickToReplyStart_ = false;
    drawContentArea();
    return true;
  }
  if (e == JoyEvent::Down || e == JoyEvent::Right) {
    if (scrollY() >= maxScroll()) {
      return false;
    }
    scrollBy(step);
    stickToReplyStart_ = false;
    drawContentArea();
    return true;
  }
  return false;
}

void AskWindow::onEnter() {
  Window::onEnter();
  clearHistory();
  errorMsg_ = "";
  pendingReply_ = false;
  contentH_ = viewportHeight();
  phase_ = AskPhase::Idle;
  autoListen_ = true; // first open → push listen window on next tick
}

void AskWindow::onExit() {
  pendingReply_ = false;
  autoListen_ = false;
}

void AskWindow::onFocus() {
  // Returning from listen window — refresh chrome/content.
  draw();
}

void AskWindow::onTick() {
  if (autoListen_) {
    autoListen_ = false;
    if (gWindows.top() == this) {
      openListen();
    }
    return;
  }

  if (pendingReply_ && phase_ == AskPhase::Thinking) {
    if (gWindows.top() != this) {
      pendingReply_ = false;
      return;
    }
    doReply();
  }
}

bool AskWindow::onEvent(JoyEvent e) {
  if (phase_ == AskPhase::Thinking) {
    if (e == JoyEvent::Back) {
      pendingReply_ = false;
      return false;
    }
    if (scrollAsk(e)) {
      return true;
    }
    return true;
  }

  if (phase_ == AskPhase::Error) {
    if (e == JoyEvent::Ok) {
      errorMsg_ = "";
      goIdle();
      return true;
    }
    if (e == JoyEvent::Back) {
      return false;
    }
    if (scrollAsk(e)) {
      return true;
    }
    return true;
  }

  if (e == JoyEvent::Ok) {
    openListen();
    return true;
  }
  if (e == JoyEvent::Back) {
    return false;
  }
  if (scrollAsk(e)) {
    return true;
  }
  return false;
}

void AskWindow::drawContent(int ox, int oy) {
  const int maxW = tft.width() - 2 * kUiPadX;
  const int textX = ox + kUiPadX;
  int y = oy + 6;
  const int viewTop = contentTop();
  const int viewBot = tft.height();

  TextStyle labelSt;
  labelSt.size = kAskLabelSize;
  labelSt.flags = TextFlagNoWrap | TextFlagTruncate;
  labelSt.maxLines = 1;
  labelSt.clipTop = contentTop();
  labelSt.clipBottom = tft.height();

  for (int i = 0; i < msgCount_; ++i) {
    const bool isUser = msgs_[i].isUser;
    const int labelH = textLineH(kAskLabelSize);
    const int bh = msgHeight(i, maxW);
    const int blockH = labelH + 2 + bh + 8;

    // Skip draw work for blocks fully outside the viewport (still advance y).
    const bool visible = (y + blockH > viewTop) && (y < viewBot);
    if (visible) {
      labelSt.color = isUser ? kUserLabel : kAiLabel;
      Text::draw(isUser ? "You" : "AI", textX, y, maxW, labelSt);
    }
    y += labelH + 2;

    if (visible && msgs_[i].text) {
      drawMsg(textX, y, maxW, msgs_[i].text, isUser ? kUserText : kAiText);
    }
    y += bh + 8;
  }

  // Live streamed assistant text (before it is committed to history).
  if (phase_ == AskPhase::Thinking && streamText_ && streamLen_ > 0) {
    labelSt.color = kAiLabel;
    const int labelH = textLineH(kAskLabelSize);
    // Estimate while streaming — full measure of 18k chars stalls the UI.
    const int bh = estimateMsgH(streamText_, maxW);
    const int blockH = labelH + 2 + bh + 8;
    const bool visible = (y + blockH > viewTop) && (y < viewBot);
    if (visible) {
      Text::draw("AI", textX, y, maxW, labelSt);
    }
    y += labelH + 2;
    if (visible) {
      drawMsg(textX, y, maxW, streamText_, kAiText);
    }
    y += bh + 8;
  }

  if (phase_ == AskPhase::Error) {
    TextStyle err;
    err.size = kAskLabelSize;
    err.color = ILI9341_RED;
    err.flags = TextFlagWrap;
    err.clipTop = (int16_t)contentTop();
    err.clipBottom = (int16_t)tft.height();
    const char *msg = errorMsg_.length() ? errorMsg_.c_str() : "Error";
    TextMetrics em = Text::draw(msg, textX, y, maxW, err);
    y = em.nextY + 4;
    TextStyle hint;
    hint.size = kAskLabelSize;
    hint.color = ILI9341_DARKGREY;
    hint.flags = TextFlagWrap;
    hint.clipTop = (int16_t)contentTop();
    hint.clipBottom = (int16_t)tft.height();
    TextMetrics hm =
        Text::draw("ok = dismiss   back = home", textX, y, maxW, hint);
    y = hm.nextY;
  } else if (phase_ == AskPhase::Idle && msgCount_ == 0) {
    textDrawCenteredHint("select to speak", contentTop(), kUiBodySize,
                         ILI9341_DARKGREY);
    y = oy + viewportHeight();
  }

  contentH_ = y - oy + 4;
  if (contentH_ < viewportHeight()) {
    contentH_ = viewportHeight();
  }
}

Window *windowAsk() { return &sAsk; }
