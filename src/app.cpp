#include "app.h"
#include "config.h"
#include "display.h"

/*
 * =============================================================================
 * PRODUCT APP (talk / gallery / Wi-Fi / Deepgram) — PARKED
 * Re-enable later by flipping this to 1 and removing the UI-shell stubs below.
 * =============================================================================
 */
#if 0 // PRODUCT:

#include "mic.h"
#include "deepgram.h"
#include "net_services.h"
#include "gallery.h"

enum class AppUi : uint8_t { Idle, Listening, Gallery };

static AppUi ui = AppUi::Idle;
static String serialLine;
static uint32_t listenStartedMs = 0;
static uint32_t lastTranscriptDrawMs = 0;

static bool btnStablePressed = false;
static bool btnLastRawPressed = false;
static uint32_t btnLastChangeMs = 0;
static bool btnIgnoreUntilRelease = false;
static uint32_t btnDownMs = 0;
static bool btnLongFired = false;

static int galleryTotal = 0;

static void showIdle() {
  ui = AppUi::Idle;
  closeDeepgram();
  stopI2S();
  micResetSession();
  serialLine = "";
  reclaimSerialUart();
  reclaimDisplay();
  showIdleScreen();
}

static void showGalleryFrame() {
  reclaimDisplay();
  if (!galleryDrawGrid()) {
    showStatus("Gallery failed", "hold to exit");
  }
}

static void enterGallery() {
  if (ui == AppUi::Listening) {
    return;
  }
  closeDeepgram();
  stopI2S();
  ui = AppUi::Gallery;
  galleryTotal = galleryCount();
  Serial.printf("gallery open count=%d\n", galleryTotal);
  showGalleryFrame();
  btnIgnoreUntilRelease = true;
}

static void galleryNextOrExit() {
  showIdle();
}

static void finishListen() {
  ui = AppUi::Idle;
  micFlushPending(flushPcmToDeepgram);
  stopI2S();
  if (digitalRead(BTN_PIN) == LOW) {
    btnIgnoreUntilRelease = true;
  }
  reclaimDisplay();
  showStatus("Finishing...");

  deepgramFinalizeAndClose();

  if (deepgramInterimText().length()) {
    if (deepgramFinalText().length()) {
      deepgramFinalText() += ' ';
    }
    deepgramFinalText() += deepgramInterimText();
    deepgramInterimText() = "";
  }

  String combined = deepgramFinalText();

  Serial.printf("session peak=%d rawPeak=%ld ch=%s\n", (int)micSessionPeak(),
                (long)micRawPeakAbs(), micChannelIndex() ? "R" : "L");
  Serial.print("Transcript: ");
  Serial.println(combined.length() ? combined : "(empty)");

  reclaimDisplay();
  if (combined.length() == 0) {
    if (micSessionPeak() < 500) {
      showStatus("Too quiet", "speak closer");
    } else {
      showStatus("Empty transcript");
    }
    delay(2500);
    showIdle();
    return;
  }

  showWrappedText("Transcript", combined.c_str());
  delay(1200);

  Serial.printf("pre-image heap=%u\n", (unsigned)ESP.getFreeHeap());
  if (!generateAndShowImage(combined)) {
    reclaimDisplay();
    showWrappedText("Image failed", combined.c_str());
    delay(4000);
  } else {
    delay(IMAGE_HOLD_MS);
  }
  showIdle();
}

static void startListen() {
  if (ui == AppUi::Listening) {
    Serial.println("Already listening");
    return;
  }
  if (ui == AppUi::Gallery) {
    return;
  }
  while (Serial.available()) {
    Serial.read();
  }
  serialLine = "";
  micResetSession();
  deepgramClearText();
  displayResetTranscriptCache();
  micSetDiscardSamples(SAMPLE_RATE / 4);
  resetAudioDsp();

  reclaimDisplay();
  showStatus("Mic probe...");
  if (!chooseMicChannel()) {
    reclaimDisplay();
    showStatus("I2S init failed");
    Serial.println("ERR i2s");
    showIdle();
    return;
  }

  reclaimDisplay();
  showStatus("Deepgram...");
  resetAudioDsp();
  if (!connectDeepgram()) {
    stopI2S();
    reclaimDisplay();
    showStatus("STT connect fail", "check API key");
    delay(2500);
    showIdle();
    return;
  }

  ui = AppUi::Listening;
  listenStartedMs = millis();
  lastTranscriptDrawMs = 0;
  reclaimDisplay();
  showListeningUi();
  Serial.println("OK listening — tap again to stop");
}

static void stopListen() {
  if (ui != AppUi::Listening) {
    Serial.println("Not listening");
    return;
  }
  Serial.println("OK stop");
  finishListen();
}

static void onTap() {
  if (ui == AppUi::Gallery) {
    Serial.println("BTN tap — gallery next/exit");
    galleryNextOrExit();
    return;
  }
  if (ui == AppUi::Listening) {
    Serial.println("BTN tap — stop");
    stopListen();
    return;
  }
  Serial.println("BTN tap — listen");
  startListen();
}

static void onLongPress() {
  if (ui == AppUi::Listening) {
    return;
  }
  if (ui == AppUi::Gallery) {
    Serial.println("BTN long — exit gallery");
    showIdle();
    btnIgnoreUntilRelease = true;
    return;
  }
  Serial.println("BTN long — gallery");
  enterGallery();
}

static void pollButton() {
  uint32_t now = millis();
  bool rawPressed = digitalRead(BTN_PIN) == LOW;
  if (rawPressed != btnLastRawPressed) {
    btnLastRawPressed = rawPressed;
    btnLastChangeMs = now;
  }
  if ((now - btnLastChangeMs) < BTN_DEBOUNCE_MS) {
    return;
  }

  if (rawPressed != btnStablePressed) {
    btnStablePressed = rawPressed;
    if (btnIgnoreUntilRelease) {
      if (!btnStablePressed) {
        btnIgnoreUntilRelease = false;
      }
      return;
    }
    if (btnStablePressed) {
      btnDownMs = now;
      btnLongFired = false;
    } else {
      if (!btnLongFired) {
        onTap();
      }
    }
    return;
  }

  if (btnIgnoreUntilRelease || !btnStablePressed || btnLongFired) {
    return;
  }
  if ((now - btnDownMs) >= BTN_LONG_MS) {
    btnLongFired = true;
    onLongPress();
  }
}

static void handleCommand(const String &cmdIn) {
  String cmd = cmdIn;
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) {
    return;
  }
  if (cmd == "listen") {
    if (ui == AppUi::Listening) {
      stopListen();
    } else {
      startListen();
    }
  } else if (cmd == "stop") {
    stopListen();
  } else if (cmd == "gallery") {
    enterGallery();
  } else if (cmd == "help" || cmd == "status") {
    Serial.printf("state=%s dg=%s finals=%u gallery=%d\n",
                  ui == AppUi::Listening ? "listening"
                  : ui == AppUi::Gallery ? "gallery"
                                         : "idle",
                  deepgramConnected() ? "up" : "down",
                  (unsigned)deepgramFinalLength(), galleryCount());
    Serial.printf("commands: listen | stop | gallery | help\n");
    Serial.printf("button: tap=talk toggle, hold=%ums gallery (GPIO%d)\n",
                  (unsigned)BTN_LONG_MS, BTN_PIN);
  }
}

static void pollSerialCommands() {
  while (Serial.available()) {
    int raw = Serial.read();
    if (raw < 0) {
      break;
    }
    char c = (char)raw;
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleCommand(serialLine);
      serialLine = "";
      continue;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      if (serialLine.length() < 16) {
        serialLine += c;
      }
    } else {
      serialLine = "";
    }
  }
}

void appSetup() {
  Serial.begin(115200);
#if defined(CHITRAM_BOARD_S3)
  Serial.setTxTimeoutMs(0);
#endif
  delay(500);
  reclaimSerialUart();
  Serial.println();
  Serial.println("BOOT1 serial-only");
  delay(200);

  Serial.printf("chitram Deepgram STT  free=%u btn=GPIO%d\n",
                (unsigned)ESP.getFreeHeap(), BTN_PIN);
#if defined(CHITRAM_BOARD_S3)
  Serial.printf("board=ESP32-S3 CAM  lcd MOSI=%d SCLK=%d CS=%d\n", TFT_MOSI,
                TFT_SCLK, TFT_CS);
  Serial.printf("mic SCK=%d WS=%d SD=%d  psram=%u\n", I2S_SCK, I2S_WS, I2S_SD,
                (unsigned)ESP.getPsramSize());
#endif

  pinMode(BTN_PIN, INPUT_PULLUP);
  btnLastRawPressed = digitalRead(BTN_PIN) == LOW;
  btnStablePressed = btnLastRawPressed;
  btnLastChangeMs = millis();
  btnIgnoreUntilRelease = btnStablePressed;

  displayBegin();
  reclaimSerialUart();
  Serial.println("BOOT3 lcd ok");
  Serial.printf("BOOT4 stream_chunk=%uB free=%u\n",
                (unsigned)(STREAM_CHUNK_SAMPLES * sizeof(int16_t)),
                (unsigned)ESP.getFreeHeap());

  if (!connectWifi()) {
    while (true) {
      delay(1000);
    }
  }
  reclaimSerialUart();
  Serial.println("BOOT5 wifi ok");

  if (littlefsBegin()) {
    galleryEnsureDir();
    Serial.printf("gallery images=%d\n", galleryCount());
  }

  showIdle();
  Serial.printf("tap GPIO%d talk · hold %ums gallery\n", BTN_PIN,
                (unsigned)BTN_LONG_MS);
}

void appLoop() {
  pollSerialCommands();
  pollButton();

  if (ui == AppUi::Listening) {
    deepgramPoll();

    if (!deepgramConnected() || !deepgramSocketAvailable()) {
      Serial.println("ERR Deepgram disconnected");
      reclaimDisplay();
      showStatus("STT dropped");
      delay(1500);
      finishListen();
      return;
    }

    if (!pollMicAndStream()) {
      Serial.println("ERR mic/stream");
      reclaimDisplay();
      showStatus("Mic/stream error");
      delay(1500);
      finishListen();
      return;
    }

    refreshVuIfNeeded(true);

    uint32_t now = millis();
    if (now - lastTranscriptDrawMs > 300) {
      lastTranscriptDrawMs = now;
      drawLiveTranscript(deepgramFinalText(), deepgramInterimText());
    }

    if (now - listenStartedMs >= MAX_LISTEN_MS) {
      Serial.println("Max listen time — auto stop");
      finishListen();
    }
  } else if (ui == AppUi::Idle) {
    static uint32_t lastBeat = 0;
    if (millis() - lastBeat > 5000) {
      lastBeat = millis();
      Serial.println("idle");
    }
  }
  delay(2);
}

#else // UI shell (step 1+)

#include "input.h"
#include "window.h"
#include "windows.h"
#include "gallery_window.h"
#include "storage.h"
#include "settings.h"
#include "profiles.h"
#include "status_window.h"

// -----------------------------------------------------------------------------
// UI shell: splash → Home → Make Photo / Ask / Profiles / …
// -----------------------------------------------------------------------------

void appSetup() {
  Serial.begin(115200);
  delay(800);
  inputBegin();

  inputLog("chitram ui — Make Photo + Ask + Profiles + Gallery + Settings");
  displayBegin();
  reclaimDisplay();
  storageBegin();
  settingsBegin();
  profilesBegin();

  gWindows.clear();
  gWindows.push(windowBootSplash());
  inputLog("boot splash — %s", settingsDeviceName());
}

void appLoop() {
  JoyEvent e = inputPoll();
  if (e != JoyEvent::None) {
    inputLog("joy: %s depth=%d", joyEventName(e), gWindows.depth());
    gWindows.dispatch(e);
  }
  gWindows.tick();
  delay(2);
}

#endif // PRODUCT
