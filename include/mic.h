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

using MicFlushFn = bool (*)(const int16_t *samples, size_t count);

// Register flush used by the egress task (SD buffer + Deepgram send).
void micSetStreamFlush(MicFlushFn fn);

// Non-blocking when egress is running (UI-safe).
bool micPollAndMaybeFlush(MicFlushFn flushCb);

size_t micPendingSamples();
// Stop capture, drain ring via egress, wait until empty.
bool micFlushPending(MicFlushFn flushCb);

// Capture (core 1) + egress (core 0) tasks.
void micStartCapture();
void micStopCapture();
void micStartEgress();
void micStopEgressAndDrain();
