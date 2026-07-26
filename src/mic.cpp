#include "mic.h"
#include "config.h"
#include "display.h"

#include <math.h>
#include <driver/i2s.h>
#include <driver/uart.h>

static int16_t streamBuf[STREAM_CHUNK_SAMPLES];
static size_t streamCount = 0;
static bool i2sReady = false;

static int32_t rawPeakAbs = 0;
static int micChannel = 0;
static int16_t livePeak = 0;
static int16_t sessionPeak = 0;
static uint32_t lastVuMs = 0;
static size_t discardSamples = 0;

static float dspDcX = 0, dspDcY = 0;
static float dspHpfX = 0, dspHpfY = 0;
static float dspPreX = 0;
static float dspPeakEma = 200000.0f;
static float dspGain = 4.0f;

void resetAudioDsp() {
  dspDcX = dspDcY = 0;
  dspHpfX = dspHpfY = 0;
  dspPreX = 0;
  dspPeakEma = 200000.0f;
  dspGain = 4.0f;
}

bool initI2S(bool rightChannel, int i2sFormat) {
  if (i2sReady) {
    i2s_driver_uninstall(I2S_PORT);
    i2sReady = false;
  }
  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format =
          rightChannel ? I2S_CHANNEL_FMT_ONLY_RIGHT : I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = (i2sFormat == 1) ? I2S_COMM_FORMAT_STAND_MSB
                                               : I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 12,
      .dma_buf_len = 256,
#if defined(CHITRAM_BOARD_S3)
      .use_apll = false,
#else
      .use_apll = true,
#endif
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  i2s_pin_config_t pins = {
      .bck_io_num = I2S_SCK,
      .ws_io_num = I2S_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_SD,
  };

  if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
    Serial.println("I2S install failed");
    return false;
  }
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    Serial.println("I2S set_pin failed");
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }
  i2s_zero_dma_buffer(I2S_PORT);
  i2sReady = true;
  return true;
}

void stopI2S() {
  if (i2sReady) {
    i2s_driver_uninstall(I2S_PORT);
    i2sReady = false;
  }
#if !defined(CHITRAM_BOARD_S3)
  // Classic DevKit: restore UART0. On S3, GPIO1 is TFT_BL (USB CDC for Serial).
  uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#endif
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
}

void reclaimSerialUart() {
#if !defined(CHITRAM_BOARD_S3)
  uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#endif
  while (Serial.available()) {
    Serial.read();
  }
}

static int16_t micSlotToPcm(int32_t raw) {
  float x = (float)(raw >> 8);

  float y = x - dspDcX + 0.995f * dspDcY;
  dspDcX = x;
  dspDcY = y;

  float hp = 0.961f * (dspHpfY + y - dspHpfX);
  dspHpfX = y;
  dspHpfY = hp;

  float pe = hp - 0.92f * dspPreX;
  dspPreX = hp;

  float absv = fabsf(pe);
  if (absv > dspPeakEma) {
    dspPeakEma = 0.85f * dspPeakEma + 0.15f * absv;
  } else {
    dspPeakEma = 0.9997f * dspPeakEma + 0.0003f * absv;
  }

  float desired = (AGC_TARGET_PEAK * 256.0f) / (dspPeakEma + 1.0f);
  if (desired < AGC_GAIN_MIN) {
    desired = AGC_GAIN_MIN;
  }
  if (desired > AGC_GAIN_MAX) {
    desired = AGC_GAIN_MAX;
  }
  dspGain += 0.0025f * (desired - dspGain);

  float out = pe * dspGain / 256.0f;

  if (out > 28000.0f) {
    out = 28000.0f + (out - 28000.0f) * 0.25f;
  } else if (out < -28000.0f) {
    out = -28000.0f + (out + 28000.0f) * 0.25f;
  }
  if (out > 32767.0f) {
    out = 32767.0f;
  }
  if (out < -32768.0f) {
    out = -32768.0f;
  }
  return (int16_t)out;
}

struct MicProbeStats {
  int32_t rawPeak = 0;
  int16_t pcmPeak = 0;
  int16_t pcmRms = 0;
  uint32_t samples = 0;
  uint32_t clipped = 0;
};

static MicProbeStats measureChannel(bool rightChannel, int i2sFormat = 0) {
  MicProbeStats st;
  resetAudioDsp();
  if (!initI2S(rightChannel, i2sFormat)) {
    return st;
  }
  delay(40);
  i2s_zero_dma_buffer(I2S_PORT);

  int64_t sumSq = 0;
  uint32_t end = millis() + 180;
  int32_t raw[128];
  while (millis() < end) {
    size_t bytesRead = 0;
    if (i2s_read(I2S_PORT, raw, sizeof(raw), &bytesRead, 40) != ESP_OK) {
      continue;
    }
    size_t n = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < n; ++i) {
      int32_t ra = raw[i] < 0 ? -raw[i] : raw[i];
      if (ra > st.rawPeak) {
        st.rawPeak = ra;
      }
      int16_t s = micSlotToPcm(raw[i]);
      int16_t a = s < 0 ? (int16_t)(-s) : s;
      if (a > st.pcmPeak) {
        st.pcmPeak = a;
      }
      if (a > 30000) {
        st.clipped++;
      }
      sumSq += (int32_t)s * (int32_t)s;
      st.samples++;
    }
  }
  if (st.samples) {
    st.pcmRms = (int16_t)sqrt((double)sumSq / (double)st.samples);
  }
  return st;
}

static int scoreMicStats(const MicProbeStats &st) {
  if (st.samples < 100 || st.pcmPeak < 50) {
    return 0;
  }
  int clipPct = (int)((st.clipped * 100) / st.samples);
  if (clipPct > 25) {
    return 0;
  }
  if (st.pcmPeak > 20000 && st.pcmRms > (st.pcmPeak * 8) / 10) {
    return 0;
  }
  return (int)st.pcmRms + st.pcmPeak / 4;
}

static void printMicStats(const char *tag, const MicProbeStats &st, int score) {
  unsigned clipPct =
      st.samples ? (unsigned)((st.clipped * 100) / st.samples) : 0;
  Serial.printf("mic %s peak=%d rms=%d clip=%u%% raw=%ld score=%d\n", tag,
                (int)st.pcmPeak, (int)st.pcmRms, clipPct, (long)st.rawPeak,
                score);
}

bool chooseMicChannel() {
  static int i2sFormat = 0;

  MicProbeStats left = measureChannel(false, i2sFormat);
  int scoreL = scoreMicStats(left);
  printMicStats("L", left, scoreL);

  stopI2S();
  MicProbeStats right = measureChannel(true, i2sFormat);
  int scoreR = scoreMicStats(right);
  printMicStats("R", right, scoreR);

  bool useRight = (scoreL == 0 && scoreR > 0);
  if (scoreL > 0 && scoreR > scoreL * 2) {
    useRight = true;
  }

  micChannel = useRight ? 1 : 0;

  if (!useRight) {
    stopI2S();
    if (scoreL == 0) {
      i2sFormat = 1 - i2sFormat;
      Serial.printf("retry I2S format=%s\n", i2sFormat ? "MSB" : "Philips");
      left = measureChannel(false, i2sFormat);
      scoreL = scoreMicStats(left);
      printMicStats("L2", left, scoreL);
      if (scoreL == 0) {
        Serial.println("WARN: mic looks dead/corrupt — check L/R=GND, SD=33");
      }
    } else if (!initI2S(false, i2sFormat)) {
      return false;
    }
  }

  Serial.printf("using mic channel %s (L/R should be %s)\n",
                micChannel ? "RIGHT" : "LEFT",
                micChannel ? "3V3" : "GND");

  if (scoreL == 0 && scoreR == 0) {
    Serial.println("WARN: both channels silent or rail-clipped junk");
  } else if (!useRight && right.pcmPeak > 20000 && left.pcmPeak < 100) {
    Serial.println("HINT: RIGHT is loud junk, LEFT quiet — solder L/R to GND");
  }

  delay(20);
  i2s_zero_dma_buffer(I2S_PORT);
  rawPeakAbs = 0;
  return true;
}

void refreshVuIfNeeded(bool listening) {
  if (!listening) {
    return;
  }
  uint32_t now = millis();
  if (now - lastVuMs < 200) {
    return;
  }
  lastVuMs = now;
  int peak = micTakeLivePeak();
  digitalWrite(TFT_CS, HIGH);
  drawVuMeter(peak);
  int bars = peak / 400;
  if (bars > 20) {
    bars = 20;
  }
  Serial.print("lvl ");
  for (int i = 0; i < bars; ++i) {
    Serial.print('#');
  }
  for (int i = bars; i < 20; ++i) {
    Serial.print('.');
  }
  Serial.printf(" %d g=%.1f%s\n", peak, dspGain,
                peak >= 500 ? " OK" : " quiet");
}

bool micIsReady() { return i2sReady; }

void micResetSession() {
  streamCount = 0;
  rawPeakAbs = 0;
  livePeak = 0;
  sessionPeak = 0;
  lastVuMs = 0;
}

void micSetDiscardSamples(size_t n) { discardSamples = n; }

int micChannelIndex() { return micChannel; }

int16_t micSessionPeak() { return sessionPeak; }

int16_t micTakeLivePeak() {
  int16_t p = livePeak;
  livePeak = 0;
  return p;
}

int32_t micRawPeakAbs() { return rawPeakAbs; }

float micDspGain() { return dspGain; }

size_t micPendingSamples() { return streamCount; }

bool micFlushPending(MicFlushFn flushCb) {
  if (streamCount == 0) {
    return true;
  }
  if (!flushCb(streamBuf, streamCount)) {
    streamCount = 0;
    return false;
  }
  streamCount = 0;
  return true;
}

bool micPollAndMaybeFlush(MicFlushFn flushCb) {
  if (!i2sReady) {
    return false;
  }
  int32_t raw[128];
  size_t bytesRead = 0;
  esp_err_t err = i2s_read(I2S_PORT, raw, sizeof(raw), &bytesRead, 0);
  if (err == ESP_ERR_TIMEOUT || bytesRead == 0) {
    return true;
  }
  if (err != ESP_OK) {
    Serial.printf("i2s_read err=%d\n", (int)err);
    return false;
  }

  size_t n = bytesRead / sizeof(int32_t);
  for (size_t i = 0; i < n; ++i) {
    int32_t a = raw[i] < 0 ? -raw[i] : raw[i];
    if (a > rawPeakAbs) {
      rawPeakAbs = a;
    }
    if (discardSamples > 0) {
      discardSamples--;
      continue;
    }
    int16_t s = micSlotToPcm(raw[i]);
    int16_t absS = s < 0 ? (int16_t)(-s) : s;
    if (absS > livePeak) {
      livePeak = absS;
    }
    if (absS > sessionPeak) {
      sessionPeak = absS;
    }
    streamBuf[streamCount++] = s;
    if (streamCount >= STREAM_CHUNK_SAMPLES) {
      if (!flushCb(streamBuf, streamCount)) {
        streamCount = 0;
        return false;
      }
      streamCount = 0;
    }
  }
  return true;
}
