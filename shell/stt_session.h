#pragma once

#include <Arduino.h>

enum class SttState : uint8_t {
  Idle,
  Connecting,
  Listening,
  Done,
  Error,
};

// Called on the main task; transcript is valid until the next sttSession call.
using SttDoneCb  = void (*)(void *ctx, const String &transcript);
using SttErrorCb = void (*)(void *ctx, const char *reason);

// Reusable STT lifecycle: WiFi → mic probe → Deepgram connect → capture.
// Silence detection + utterance-end auto-stop included.
// Call tick() every loop iteration while state is Listening.
class SttSession {
public:
  SttState state() const { return state_; }
  // Live text during Listening (interim + finals). Safe to call from main task.
  String liveText() const;
  // Transcript after Done.
  const String &transcript() const { return transcript_; }

  // Start a new STT session. draw callback is called after state changes so
  // the host can redraw before blocking operations (WiFi / Deepgram connect).
  void start(SttDoneCb doneCb, SttErrorCb errorCb, void *ctx,
             void (*drawCb)(void *ctx));
  // Stop: flush, finalize Deepgram, snapshot transcript → calls doneCb.
  void stop();
  // Abort: teardown without callback.
  void abort();
  // Call every loop iteration while Listening.
  void tick();

private:
  SttState state_ = SttState::Idle;
  SttDoneCb doneCb_ = nullptr;
  SttErrorCb errorCb_ = nullptr;
  void (*drawCb_)(void *) = nullptr;
  void *ctx_ = nullptr;

  uint32_t listenStartedMs_ = 0;
  uint32_t lastSoundMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  bool heardSound_ = false;
  String lastActivityText_;
  String transcript_;

  void teardown(bool finalize);
  void fail(const char *reason);
};

extern SttSession gSttSession;
