#include "deepgram.h"
#include "audio_rec.h"
#include "config.h"
#include "mic.h"
#include "secrets.h"

#include <ArduinoWebsockets.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

using namespace websockets;

static WebsocketsClient dgClient;
static volatile bool dgConnected = false;
static String finalText;
static String interimText;
static String lastFinalText;
static uint32_t lastKeepAliveMs = 0;
static uint32_t lastAudioSentMs = 0;
static bool utteranceEndPending = false;
static uint32_t dgBytesSent = 0;
static uint32_t dgSendFails = 0;

static SemaphoreHandle_t dgTextMu = nullptr;

// PCM waiting for WebSocket — egress pushes, dgIoTask pops/sends.
static int16_t *dgTxBuf = nullptr;
static size_t dgTxCap = 0;
static size_t dgTxHead = 0;
static size_t dgTxTail = 0;
static size_t dgTxUsed = 0;
static uint32_t dgTxOverflows = 0;
static portMUX_TYPE dgTxMux = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t dgIoTaskHandle = nullptr;
static volatile bool dgIoRunning = false;
static volatile bool dgIoFinalize = false;

static void dgTextLock() {
  if (dgTextMu) {
    xSemaphoreTake(dgTextMu, portMAX_DELAY);
  }
}
static void dgTextUnlock() {
  if (dgTextMu) {
    xSemaphoreGive(dgTextMu);
  }
}

String &deepgramFinalText() { return finalText; }
String &deepgramInterimText() { return interimText; }
String &deepgramLastFinalText() { return lastFinalText; }

void deepgramClearText() {
  dgTextLock();
  finalText = "";
  interimText = "";
  lastFinalText = "";
  utteranceEndPending = false;
  dgTextUnlock();
}

size_t deepgramFinalLength() {
  dgTextLock();
  size_t n = finalText.length();
  dgTextUnlock();
  return n;
}

bool deepgramTakeUtteranceEnd() {
  dgTextLock();
  bool v = utteranceEndPending;
  utteranceEndPending = false;
  dgTextUnlock();
  return v;
}

bool deepgramConnected() { return dgConnected; }
// Do not touch dgClient from the UI task while dg_io owns it.
bool deepgramSocketAvailable() { return dgConnected; }

String deepgramCopyListeningText() {
  dgTextLock();
  String t = finalText;
  if (interimText.length()) {
    if (t.length()) {
      t += ' ';
    }
    t += interimText;
  }
  dgTextUnlock();
  return t;
}

static bool dgTxAlloc() {
  if (dgTxBuf) {
    return true;
  }
  dgTxCap = MIC_RING_SAMPLES; // 4s
  size_t bytes = dgTxCap * sizeof(int16_t);
  dgTxBuf =
      (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!dgTxBuf) {
    dgTxBuf = (int16_t *)malloc(bytes);
  }
  if (!dgTxBuf) {
    Serial.printf("ERR dg tx ring alloc %u\n", (unsigned)bytes);
    dgTxCap = 0;
    return false;
  }
  dgTxHead = dgTxTail = dgTxUsed = 0;
  dgTxOverflows = 0;
  Serial.printf("dg tx ring %u samples\n", (unsigned)dgTxCap);
  return true;
}

static void dgTxReset() {
  portENTER_CRITICAL(&dgTxMux);
  dgTxHead = dgTxTail = dgTxUsed = 0;
  portEXIT_CRITICAL(&dgTxMux);
}

static size_t dgTxPush(const int16_t *samples, size_t count) {
  if (!dgTxBuf || !samples || count == 0) {
    return 0;
  }
  size_t pushed = 0;
  while (pushed < count) {
    size_t chunk = count - pushed;
    if (chunk > 64) {
      chunk = 64;
    }
    portENTER_CRITICAL(&dgTxMux);
    for (size_t i = 0; i < chunk; ++i) {
      if (dgTxUsed >= dgTxCap) {
        dgTxOverflows += (uint32_t)(count - pushed - i);
        portEXIT_CRITICAL(&dgTxMux);
        return pushed + i;
      }
      dgTxBuf[dgTxHead] = samples[pushed + i];
      dgTxHead = (dgTxHead + 1) % dgTxCap;
      dgTxUsed++;
    }
    portEXIT_CRITICAL(&dgTxMux);
    pushed += chunk;
  }
  return pushed;
}

static size_t dgTxPop(int16_t *dest, size_t maxCount) {
  size_t popped = 0;
  portENTER_CRITICAL(&dgTxMux);
  while (popped < maxCount && dgTxUsed > 0) {
    dest[popped++] = dgTxBuf[dgTxTail];
    dgTxTail = (dgTxTail + 1) % dgTxCap;
    dgTxUsed--;
  }
  portEXIT_CRITICAL(&dgTxMux);
  return popped;
}

static size_t dgTxAvailable() {
  portENTER_CRITICAL(&dgTxMux);
  size_t n = dgTxUsed;
  portEXIT_CRITICAL(&dgTxMux);
  return n;
}

static bool extractJsonStringField(const String &json, const char *key,
                                   String &out) {
  String needle = String("\"") + key + "\"";
  int keyPos = json.indexOf(needle);
  if (keyPos < 0) {
    return false;
  }
  int colon = json.indexOf(':', keyPos + needle.length());
  if (colon < 0) {
    return false;
  }
  int start = json.indexOf('"', colon + 1);
  if (start < 0) {
    return false;
  }
  start++;
  String result;
  for (int i = start; i < (int)json.length(); ++i) {
    char c = json[i];
    if (c == '\\' && i + 1 < (int)json.length()) {
      char n = json[++i];
      if (n == 'n') {
        result += '\n';
      } else if (n == 't') {
        result += '\t';
      } else if (n == '"' || n == '\\' || n == '/') {
        result += n;
      } else {
        result += n;
      }
      continue;
    }
    if (c == '"') {
      break;
    }
    result += c;
  }
  out = result;
  return true;
}

static bool jsonBoolFieldTrue(const String &json, const char *key) {
  String needle = String("\"") + key + "\"";
  int keyPos = json.indexOf(needle);
  if (keyPos < 0) {
    return false;
  }
  int colon = json.indexOf(':', keyPos + needle.length());
  if (colon < 0) {
    return false;
  }
  int i = colon + 1;
  while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\t')) {
    i++;
  }
  return json.startsWith("true", i);
}

static void onDgMessage(WebsocketsMessage message) {
  if (!message.isText()) {
    return;
  }
  String payload = message.data();

  if (payload.indexOf("\"UtteranceEnd\"") >= 0 ||
      payload.indexOf("\"type\":\"UtteranceEnd\"") >= 0) {
    dgTextLock();
    if (interimText.length()) {
      if (finalText.length()) {
        finalText += ' ';
      }
      finalText += interimText;
      lastFinalText = interimText;
      Serial.print("UTTERANCE: ");
      Serial.println(interimText);
      interimText = "";
    }
    utteranceEndPending = true;
    dgTextUnlock();
    Serial.println("UtteranceEnd");
    return;
  }

  String transcript;
  if (!extractJsonStringField(payload, "transcript", transcript)) {
    return;
  }
  if (transcript.length() == 0) {
    return;
  }

  bool isFinal = jsonBoolFieldTrue(payload, "is_final");
  dgTextLock();
  if (isFinal) {
    if (finalText.length()) {
      finalText += ' ';
    }
    finalText += transcript;
    lastFinalText = transcript;
    interimText = "";
    Serial.print("FINAL: ");
    Serial.println(transcript);
  } else {
    interimText = transcript;
    Serial.print("....: ");
    Serial.println(transcript);
  }
  dgTextUnlock();
}

static void onDgEvent(WebsocketsEvent event, String data) {
  (void)data;
  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("Deepgram WS open");
    dgConnected = true;
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("Deepgram WS closed");
    dgConnected = false;
  } else if (event == WebsocketsEvent::GotPing) {
    dgClient.pong();
  }
}

static void dgIoTask(void *arg) {
  (void)arg;
  int16_t sendBuf[STREAM_CHUNK_SAMPLES];
  uint32_t lastStatMs = millis();

  while (dgIoRunning || dgTxAvailable() > 0) {
    bool didWork = false;

    if (dgConnected && dgClient.available()) {
      // Send as many queued chunks as are ready (keep up with realtime).
      for (int i = 0; i < 8; ++i) {
        size_t n = dgTxPop(sendBuf, STREAM_CHUNK_SAMPLES);
        if (n == 0) {
          break;
        }
        size_t bytes = n * sizeof(int16_t);
        bool ok = dgClient.sendBinary((const char *)sendBuf, bytes);
        if (!ok) {
          dgSendFails++;
          Serial.printf("ERR Deepgram sendBinary failed (fails=%lu)\n",
                        (unsigned long)dgSendFails);
          dgConnected = false;
          break;
        }
        dgBytesSent += (uint32_t)bytes;
        lastAudioSentMs = millis();
        didWork = true;
        dgClient.poll();
      }

      dgClient.poll();

      uint32_t now = millis();
      if (dgConnected && now - lastAudioSentMs > 3000 &&
          now - lastKeepAliveMs > 3000) {
        dgClient.send("{\"type\":\"KeepAlive\"}");
        lastKeepAliveMs = now;
      }

      if (now - lastStatMs >= 1000) {
        lastStatMs = now;
        Serial.printf("dg sent=%luB txq=%u fails=%lu\n",
                      (unsigned long)dgBytesSent, (unsigned)dgTxAvailable(),
                      (unsigned long)dgSendFails);
      }
    } else if (!dgIoRunning) {
      break;
    }

    if (!didWork) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }

  if (dgIoFinalize && dgClient.available()) {
    // Drain any last samples
    while (dgTxAvailable() > 0) {
      size_t n = dgTxPop(sendBuf, STREAM_CHUNK_SAMPLES);
      if (n == 0) {
        break;
      }
      dgClient.sendBinary((const char *)sendBuf, n * sizeof(int16_t));
      dgClient.poll();
    }
    dgClient.send("{\"type\":\"Finalize\"}");
    uint32_t deadline = millis() + 800;
    while (millis() < deadline && dgClient.available()) {
      dgClient.poll();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    dgClient.send("{\"type\":\"CloseStream\"}");
    deadline = millis() + 400;
    while (millis() < deadline && dgClient.available()) {
      dgClient.poll();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    dgClient.close();
  }

  dgConnected = false;
  dgIoTaskHandle = nullptr;
  vTaskDelete(NULL);
}

static void dgIoStop(bool finalize) {
  dgIoFinalize = finalize;
  dgIoRunning = false;
  for (int i = 0; i < 1500 && dgIoTaskHandle; ++i) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (dgIoTaskHandle) {
    vTaskDelete(dgIoTaskHandle);
    dgIoTaskHandle = nullptr;
  }
  dgConnected = false;
}

void closeDeepgram() { dgIoStop(true); }

bool connectDeepgram() {
  if (String(DEEPGRAM_API_KEY).length() < 8 ||
      String(DEEPGRAM_API_KEY) == "REPLACE_ME" ||
      String(DEEPGRAM_API_KEY) == "your-deepgram-api-key") {
    Serial.println("ERR set DEEPGRAM_API_KEY in include/secrets.h");
    return false;
  }

  if (!dgTextMu) {
    dgTextMu = xSemaphoreCreateMutex();
  }
  dgBytesSent = 0;
  dgSendFails = 0;
  dgIoFinalize = false;

  // Ensure previous IO task is gone
  if (dgIoTaskHandle) {
    dgIoStop(false);
  }

  dgClient.onMessage(onDgMessage);
  dgClient.onEvent(onDgEvent);
  dgClient.setInsecure();

  static bool authHeaderAdded = false;
  if (!authHeaderAdded) {
    String auth = String("Token ") + DEEPGRAM_API_KEY;
    dgClient.addHeader("Authorization", auth);
    authHeaderAdded = true;
  }

  Serial.printf("Deepgram WSS connect heap=%u maxAlloc=%u...\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  uint32_t t0 = millis();
  String url = String("wss://") + DG_HOST + DG_PATH;
  bool ok = dgClient.connect(url);
  if (!ok) {
    Serial.printf("Deepgram connect failed after %lums\n",
                  (unsigned long)(millis() - t0));
    dgConnected = false;
    return false;
  }
  Serial.printf("Deepgram connected %lums heap=%u\n",
                (unsigned long)(millis() - t0), (unsigned)ESP.getFreeHeap());
  dgConnected = true;
  lastKeepAliveMs = millis();
  lastAudioSentMs = millis();

  // Allocate TX ring only after TLS is up.
  if (!dgTxAlloc()) {
    dgClient.close();
    dgConnected = false;
    return false;
  }
  dgTxReset();
  dgIoRunning = true;
  BaseType_t tok =
      xTaskCreatePinnedToCore(dgIoTask, "dg_io", 8192, NULL, 16,
                              &dgIoTaskHandle, 0);
  if (tok != pdPASS) {
    dgIoRunning = false;
    dgIoTaskHandle = nullptr;
    Serial.println("ERR dg io task create failed");
    dgClient.close();
    dgConnected = false;
    return false;
  }
  Serial.println("dg io task started");
  return true;
}

void deepgramPoll() {
  // WebSocket I/O owned by dg_io task.
}

// Called from mic egress task: PSRAM record + queue for dg_io send.
bool flushPcmToDeepgram(const int16_t *samples, size_t count) {
  if (count == 0) {
    return true;
  }
  if (audioRecActive() && !audioRecWrite(samples, count)) {
    Serial.println("WARN audio write failed — continuing stream");
  }
#if defined(CHITRAM_AUDIO_ONLY)
  return true;
#else
  // Only queue for Deepgram after the socket is up.
  if (!deepgramConnected()) {
    return true;
  }
  if (!dgTxAlloc()) {
    return true;
  }
  size_t n = dgTxPush(samples, count);
  if (n < count) {
    static uint32_t lastOv = 0;
    uint32_t now = millis();
    if (now - lastOv > 2000) {
      lastOv = now;
      Serial.printf("WARN dg tx overflow (total=%lu)\n",
                    (unsigned long)dgTxOverflows);
    }
  }
  return true;
#endif
}

bool pollMicAndStream() {
  micPollAndMaybeFlush(flushPcmToDeepgram);
  return true;
}

void deepgramFinalizeAndClose(uint32_t finalizeWaitMs, uint32_t closeWaitMs) {
  (void)finalizeWaitMs;
  (void)closeWaitMs;
  dgIoStop(true);
}
