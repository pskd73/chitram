#pragma once

#include <stddef.h>
#include <stdint.h>

// Record PCM16 mono (SAMPLE_RATE) — buffers in PSRAM, writes /audio/NNNNN.wav on end.
// Same samples as streamed to Deepgram.

bool audioRecBegin();
bool audioRecWrite(const int16_t *samples, size_t count);
uint32_t audioRecBytes();
// Finalize WAV to SD. pathOut optional (receives saved path).
bool audioRecEnd(char *pathOut = nullptr, size_t pathOutLen = 0);
bool audioRecActive();
