#include "stt_session.h"

#include "audio_rec.h"
#include "config.h"
#include "deepgram.h"
#include "display.h"
#include "input.h"
#include "mic.h"
#include "net_services.h"
#include "settings.h"
#include "status_window.h"

#include <WiFi.h>

SttSession gSttSession;

String SttSession::liveText() const {
  if (state_ == SttState::Listening || state_ == SttState::Connecting) {
    return deepgramCopyListeningText();
  }
  return transcript_;
}

void SttSession::start(SttDoneCb doneCb, SttErrorCb errorCb, void *ctx,
                       void (*drawCb)(void *)) {
  doneCb_  = doneCb;
  errorCb_ = errorCb;
  drawCb_  = drawCb;
  ctx_     = ctx;

  state_ = SttState::Connecting;
  transcript_ = "";
  heardSound_ = false;
  lastActivityText_ = "";
  deepgramClearText();
  displayResetTranscriptCache();

  if (drawCb_) {
    drawCb_(ctx_);
  }

#if !defined(CHITRAM_AUDIO_ONLY)
  if (WiFi.status() != WL_CONNECTED) {
    inputLog("stt: wifi...");
    gWindows.push(windowWifiConnecting());
    bool ok = connectWifi();
    gWindows.pop();
    if (!ok) {
      fail("WiFi failed");
      return;
    }
  }
#endif

  inputLog("stt: mic probe...");
  if (!chooseMicChannel()) {
    fail("Mic init failed");
    return;
  }

#if defined(CHITRAM_AUDIO_ONLY)
  inputLog("stt: AUDIO ONLY");
#else
  Serial.printf("stt pre-dg heap=%u\n", (unsigned)ESP.getFreeHeap());
  if (!connectDeepgram()) {
    stopI2S();
    fail("STT connect failed");
    return;
  }
  Serial.printf("stt post-dg heap=%u\n", (unsigned)ESP.getFreeHeap());
#endif

  if (settingsSaveAudio()) {
    if (!audioRecBegin()) {
#if defined(CHITRAM_AUDIO_ONLY)
      stopI2S();
      fail("SD record failed");
      return;
#else
      inputLog("stt: audio rec failed (continuing)");
#endif
    }
  }

  micResetSession();
  micSetDiscardSamples(SAMPLE_RATE / 4);
  resetAudioDsp();
  micSetStreamFlush(flushPcmToDeepgram);
  micStartCapture();

  state_ = SttState::Listening;
  listenStartedMs_ = millis();
  lastSoundMs_ = millis();
  heardSound_ = false;
  inputLog("stt: listening");

  if (drawCb_) {
    drawCb_(ctx_);
  }
}

void SttSession::stop() {
  if (state_ != SttState::Listening) {
    return;
  }
  inputLog("stt: stop");

  teardown(true);

  state_ = SttState::Done;
  inputLog("stt: done \"%s\"", transcript_.c_str());

  if (doneCb_) {
    doneCb_(ctx_, transcript_);
  }
}

void SttSession::abort() {
  if (state_ == SttState::Idle) {
    return;
  }
  teardown(false);
  state_ = SttState::Idle;
}

void SttSession::tick() {
  if (state_ != SttState::Listening) {
    return;
  }

#if !defined(CHITRAM_AUDIO_ONLY)
  deepgramPoll();
  if (!deepgramConnected() || !deepgramSocketAvailable()) {
    inputLog("stt: STT dropped");
    fail("STT disconnected");
    return;
  }
#endif

  if (!pollMicAndStream()) {
    inputLog("stt: mic/stream error");
    fail("Mic/stream error");
    return;
  }

  uint32_t now = millis();
  int16_t peak = micTakeLivePeak();

#if defined(CHITRAM_AUDIO_ONLY)
  const bool loudPeak = peak >= SILENCE_SOUND_PEAK;
  if (loudPeak) {
    lastSoundMs_ = now;
    heardSound_ = true;
  }
#else
  String activity = deepgramCopyListeningText();
  const bool sttGrew = activity.length() && activity != lastActivityText_;
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
    inputLog("stt: utterance end");
    stop();
    return;
  }
#endif

  if (heardSound_ && (now - lastSoundMs_) >= SILENCE_STOP_MS) {
#if !defined(CHITRAM_AUDIO_ONLY)
    inputLog("stt: silence %lums", (unsigned long)(now - lastSoundMs_));
    stop();
    return;
#endif
  }

  if (now - listenStartedMs_ >= MAX_LISTEN_MS) {
    inputLog("stt: max time");
    stop();
  }
}

// ---- private helpers ----

void SttSession::teardown(bool finalize) {
  micFlushPending(flushPcmToDeepgram);
  if (audioRecActive()) {
    char path[40];
    audioRecEnd(path, sizeof(path));
    if (path[0]) {
      inputLog("stt: saved %s", path);
    }
  }
  stopI2S();

#if !defined(CHITRAM_AUDIO_ONLY)
  if (finalize) {
    // Promote any remaining interim to final before snapshotting.
    if (deepgramInterimText().length()) {
      if (deepgramFinalText().length()) {
        deepgramFinalText() += ' ';
      }
      deepgramFinalText() += deepgramInterimText();
      deepgramInterimText() = "";
    }
    transcript_ = deepgramFinalText();
    deepgramFinalizeAndClose(400, 200);
    // Capture any late finals that arrived during finalize.
    if (deepgramInterimText().length()) {
      if (deepgramFinalText().length()) {
        deepgramFinalText() += ' ';
      }
      deepgramFinalText() += deepgramInterimText();
      deepgramInterimText() = "";
    }
    transcript_ = deepgramFinalText();
  } else {
    closeDeepgram();
    transcript_ = "";
  }
#else
  transcript_ = finalize ? "Saved to /audio (see serial)" : "";
#endif
}

void SttSession::fail(const char *reason) {
  teardown(false);
  state_ = SttState::Error;
  inputLog("stt: error %s", reason ? reason : "");
  if (errorCb_) {
    errorCb_(ctx_, reason ? reason : "Error");
  }
}
