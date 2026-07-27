#include "audio_rec.h"

#include "config.h"
#include "storage.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#define AUDIO_DIR "/audio"
#ifndef AUDIO_REC_MAX_SECONDS
#define AUDIO_REC_MAX_SECONDS 60
#endif
#define AUDIO_REC_MAX_SAMPLES (SAMPLE_RATE * AUDIO_REC_MAX_SECONDS)

static int16_t *sPcm = nullptr;
static size_t sPcmCap = 0;
static size_t sPcmCount = 0;
static bool sActive = false;
static char sPath[40] = {};

static void writeU16(File &f, uint16_t v) {
  f.write((uint8_t)(v & 0xff));
  f.write((uint8_t)((v >> 8) & 0xff));
}

static void writeU32(File &f, uint32_t v) {
  f.write((uint8_t)(v & 0xff));
  f.write((uint8_t)((v >> 8) & 0xff));
  f.write((uint8_t)((v >> 16) & 0xff));
  f.write((uint8_t)((v >> 24) & 0xff));
}

static bool ensureAudioDir() {
  if (!storageBegin()) {
    return false;
  }
  if (!imageFs().exists(AUDIO_DIR)) {
    if (!imageFs().mkdir(AUDIO_DIR)) {
      Serial.println("ERR mkdir /audio");
      return false;
    }
  }
  return true;
}

static int audioMaxSeq() {
  int maxSeq = 0;
  File dir = imageFs().open(AUDIO_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }
  File f = dir.openNextFile();
  while (f) {
    const char *name = f.name();
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (base[0] >= '0' && base[0] <= '9') {
      int v = 0;
      const char *p = base;
      while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        ++p;
        if (v > 999999) {
          v = -1;
          break;
        }
      }
      if (v > 0 && (strcasecmp(p, ".wav") == 0) && v > maxSeq) {
        maxSeq = v;
      }
    }
    f = dir.openNextFile();
  }
  dir.close();
  return maxSeq;
}

static bool allocPcmBuf() {
  if (sPcm && sPcmCap >= AUDIO_REC_MAX_SAMPLES) {
    return true;
  }
  if (sPcm) {
    free(sPcm);
    sPcm = nullptr;
    sPcmCap = 0;
  }
  size_t bytes = (size_t)AUDIO_REC_MAX_SAMPLES * sizeof(int16_t);
  sPcm = (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!sPcm) {
    sPcm = (int16_t *)malloc(bytes);
  }
  if (!sPcm) {
    Serial.printf("ERR audio pcm alloc %u\n", (unsigned)bytes);
    return false;
  }
  sPcmCap = AUDIO_REC_MAX_SAMPLES;
  Serial.printf("audio: pcm buf %u samples (%u bytes)\n", (unsigned)sPcmCap,
                (unsigned)bytes);
  return true;
}

bool audioRecActive() { return sActive; }

uint32_t audioRecBytes() { return (uint32_t)(sPcmCount * sizeof(int16_t)); }

bool audioRecBegin() {
  if (sActive) {
    audioRecEnd();
  }
  sPcmCount = 0;
  sPath[0] = '\0';

  if (!allocPcmBuf()) {
    return false;
  }
  if (!ensureAudioDir()) {
    return false;
  }

  int seq = audioMaxSeq() + 1;
  snprintf(sPath, sizeof(sPath), "%s/%05d.wav", AUDIO_DIR, seq);
  sActive = true;
  Serial.printf("audio: buffering → %s (max %ds)\n", sPath, AUDIO_REC_MAX_SECONDS);
  return true;
}

bool audioRecWrite(const int16_t *samples, size_t count) {
  if (!sActive || !samples || count == 0 || !sPcm) {
    return true;
  }
  size_t space = sPcmCap - sPcmCount;
  if (count > space) {
    count = space;
  }
  if (count == 0) {
    return true;
  }
  memcpy(sPcm + sPcmCount, samples, count * sizeof(int16_t));
  sPcmCount += count;
  return true;
}

bool audioRecEnd(char *pathOut, size_t pathOutLen) {
  if (!sActive) {
    if (pathOut && pathOutLen) {
      pathOut[0] = '\0';
    }
    return false;
  }
  sActive = false;

  uint32_t dataBytes = (uint32_t)(sPcmCount * sizeof(int16_t));
  if (!sPath[0] || dataBytes == 0) {
    Serial.println("audio: nothing to write");
    if (pathOut && pathOutLen) {
      pathOut[0] = '\0';
    }
    return false;
  }

  File f = imageFs().open(sPath, FILE_WRITE);
  if (!f) {
    Serial.printf("ERR audio open %s\n", sPath);
    if (pathOut && pathOutLen) {
      pathOut[0] = '\0';
    }
    return false;
  }

  f.print("RIFF");
  writeU32(f, 36 + dataBytes);
  f.print("WAVE");
  f.print("fmt ");
  writeU32(f, 16);
  writeU16(f, 1);
  writeU16(f, 1);
  writeU32(f, (uint32_t)SAMPLE_RATE);
  writeU32(f, (uint32_t)SAMPLE_RATE * 2);
  writeU16(f, 2);
  writeU16(f, 16);
  f.print("data");
  writeU32(f, dataBytes);

  size_t written = f.write((const uint8_t *)sPcm, dataBytes);
  f.flush();
  f.close();

  float sec = (SAMPLE_RATE > 0) ? (sPcmCount / (float)SAMPLE_RATE) : 0;
  Serial.printf("audio: saved %s bytes=%lu (%.1fs) wrote=%u\n", sPath,
                (unsigned long)dataBytes, sec, (unsigned)written);

  if (pathOut && pathOutLen) {
    strncpy(pathOut, sPath, pathOutLen - 1);
    pathOut[pathOutLen - 1] = '\0';
  }
  return written == dataBytes;
}
