#pragma once

#include "chitram_tft.h"
#include <Arduino.h>

extern ChitramTft tft;

void displayBegin();
void reclaimDisplay();
void showStatus(const char *line1, const char *line2 = nullptr);
void showWrappedText(const char *title, const char *body);
void showListeningUi();
void showIdleScreen();
void showGalleryHud(int index, int total);
void drawVuMeter(int peak);
void drawLiveTranscript(const String &finalText, const String &interimText);
void displayResetTranscriptCache();
