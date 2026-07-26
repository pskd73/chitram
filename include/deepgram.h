#pragma once

#include <Arduino.h>

bool connectDeepgram();
void closeDeepgram();
void deepgramPoll();
bool deepgramConnected();
bool deepgramSocketAvailable();

bool flushPcmToDeepgram(const int16_t *samples, size_t count);
bool pollMicAndStream();
void deepgramFinalizeAndClose(uint32_t finalizeWaitMs = 400,
                              uint32_t closeWaitMs = 200);

String &deepgramFinalText();
String &deepgramInterimText();
// Most recent is_final segment (not the full appended session transcript).
String &deepgramLastFinalText();
void deepgramClearText();
size_t deepgramFinalLength();
// True once after Deepgram UtteranceEnd (clears the latch).
bool deepgramTakeUtteranceEnd();
