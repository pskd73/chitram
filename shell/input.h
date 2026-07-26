#pragma once

#include <Arduino.h>

enum class JoyEvent : uint8_t {
  None = 0,
  Up,
  Down,
  Left,
  Right,
  Ok,
  Back,
  Screenshot, // stick up + button
};

void inputBegin();
JoyEvent inputPoll();
const char *joyEventName(JoyEvent e);
uint32_t inputRxBytes();
void inputLog(const char *fmt, ...);
