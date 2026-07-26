#pragma once

#include <Arduino.h>

// Soft clip for content draws so they never paint the fixed title bar.
// Set around Window::drawContentArea; cleared after.
void uiClipSet(int16_t top, int16_t bottom); // top inclusive, bottom exclusive
void uiClipClear();
bool uiClipActive();
int16_t uiClipTop();
int16_t uiClipBottom();

// Intersect a vertical span with the active clip (or full screen if inactive).
// Returns false if nothing visible. On true, *y0/*y1 are visible [y0, y1).
bool uiClipSpan(int16_t y, int16_t h, int16_t *y0, int16_t *y1);
