#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

void resetAudioDsp();
bool initI2S(bool rightChannel, int i2sFormat = 0);
void stopI2S();
void reclaimSerialUart();
bool chooseMicChannel();
void refreshVuIfNeeded(bool listening);

bool micIsReady();
void micResetSession();
void micSetDiscardSamples(size_t n);
int micChannelIndex();
int16_t micSessionPeak();
// Peak since last take (resets). Used for VU / silence detection.
int16_t micTakeLivePeak();
int32_t micRawPeakAbs();
float micDspGain();

// Fill internal stream buffer from I2S; may call flushCb when a chunk is full.
using MicFlushFn = bool (*)(const int16_t *samples, size_t count);
bool micPollAndMaybeFlush(MicFlushFn flushCb);

size_t micPendingSamples();
bool micFlushPending(MicFlushFn flushCb);
