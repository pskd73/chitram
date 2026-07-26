#pragma once

#include <Arduino.h>

bool connectWifi();
// Generate (or edit) image, save to gallery. Optionally draw fullscreen.
// outPath receives saved path. referencePath, if set, is sent as input_references.
bool generateAndShowImage(const String &promptIn, char *outPath = nullptr,
                          size_t outLen = 0, bool draw = true,
                          const char *referencePath = nullptr);
bool littlefsBegin();
