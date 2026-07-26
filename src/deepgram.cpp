#include "deepgram.h"
#include "config.h"
#include "mic.h"
#include "secrets.h"

#include <ArduinoWebsockets.h>

using namespace websockets;

static WebsocketsClient dgClient;
static bool dgConnected = false;
static String finalText;
static String interimText;
static String lastFinalText;
static uint32_t lastKeepAliveMs = 0;
static uint32_t lastAudioSentMs = 0;
static bool utteranceEndPending = false;

String &deepgramFinalText() { return finalText; }
String &deepgramInterimText() { return interimText; }
String &deepgramLastFinalText() { return lastFinalText; }
void deepgramClearText() {
  finalText = "";
  interimText = "";
  lastFinalText = "";
  utteranceEndPending = false;
}
size_t deepgramFinalLength() { return finalText.length(); }
bool deepgramTakeUtteranceEnd() {
  bool v = utteranceEndPending;
  utteranceEndPending = false;
  return v;
}
bool deepgramConnected() { return dgConnected; }
bool deepgramSocketAvailable() { return dgClient.available(); }

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

void closeDeepgram() {
  if (dgClient.available()) {
    dgClient.send("{\"type\":\"CloseStream\"}");
    uint32_t deadline = millis() + 1500;
    while (millis() < deadline && dgClient.available()) {
      dgClient.poll();
      delay(10);
    }
    dgClient.close();
  }
  dgConnected = false;
}

bool connectDeepgram() {
  if (String(DEEPGRAM_API_KEY).length() < 8 ||
      String(DEEPGRAM_API_KEY) == "REPLACE_ME" ||
      String(DEEPGRAM_API_KEY) == "your-deepgram-api-key") {
    Serial.println("ERR set DEEPGRAM_API_KEY in include/secrets.h");
    return false;
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

  Serial.printf("Deepgram WSS connect heap=%u...\n",
                (unsigned)ESP.getFreeHeap());
  uint32_t t0 = millis();
  String url = String("wss://") + DG_HOST + DG_PATH;
  bool ok = dgClient.connect(url);
  if (!ok) {
    Serial.printf("Deepgram connect failed after %lums\n",
                  (unsigned long)(millis() - t0));
    dgConnected = false;
    return false;
  }
  Serial.printf("Deepgram connected %lums\n", (unsigned long)(millis() - t0));
  dgConnected = true;
  lastKeepAliveMs = millis();
  lastAudioSentMs = millis();
  return true;
}

void deepgramPoll() {
  dgClient.poll();

  uint32_t now = millis();
  if (dgConnected && now - lastAudioSentMs > 3000 &&
      now - lastKeepAliveMs > 3000) {
    dgClient.send("{\"type\":\"KeepAlive\"}");
    lastKeepAliveMs = now;
  }
}

bool flushPcmToDeepgram(const int16_t *samples, size_t count) {
  if (count == 0 || !dgConnected) {
    return true;
  }
  size_t bytes = count * sizeof(int16_t);
  bool ok = dgClient.sendBinary((const char *)samples, bytes);
  if (!ok) {
    Serial.println("ERR Deepgram sendBinary failed");
    dgConnected = false;
    return false;
  }
  lastAudioSentMs = millis();
  return true;
}

bool pollMicAndStream() {
  return micPollAndMaybeFlush(flushPcmToDeepgram);
}

void deepgramFinalizeAndClose(uint32_t finalizeWaitMs, uint32_t closeWaitMs) {
  if (!dgClient.available()) {
    dgConnected = false;
    return;
  }
  dgClient.send("{\"type\":\"Finalize\"}");
  uint32_t deadline = millis() + finalizeWaitMs;
  while (millis() < deadline && dgClient.available()) {
    dgClient.poll();
    delay(5);
  }

  dgClient.send("{\"type\":\"CloseStream\"}");
  deadline = millis() + closeWaitMs;
  while (millis() < deadline && dgClient.available()) {
    dgClient.poll();
    delay(5);
  }
  dgClient.close();
  dgConnected = false;
}
