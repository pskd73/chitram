#include "input.h"
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(CHITRAM_BOARD_S3)
#include "driver/usb_serial_jtag.h"
#include "esp_rom_sys.h"
static bool s_jtag = false;
static int sJoyCx = 1955; // measured at rest on 3V3
static int sJoyCy = 1880;
#endif

static String joyLine;
static uint32_t rxBytes = 0;

uint32_t inputRxBytes() { return rxBytes; }

void inputLog(const char *fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
#if defined(CHITRAM_BOARD_S3)
  if (s_jtag) {
    usb_serial_jtag_write_bytes(buf, strlen(buf), 0);
    usb_serial_jtag_write_bytes("\n", 1, 0);
  } else {
    esp_rom_printf("%s\n", buf);
  }
#else
  Serial.println(buf);
#endif
}

const char *joyEventName(JoyEvent e) {
  switch (e) {
  case JoyEvent::Up:
    return "up";
  case JoyEvent::Down:
    return "down";
  case JoyEvent::Left:
    return "left";
  case JoyEvent::Right:
    return "right";
  case JoyEvent::Ok:
    return "ok";
  case JoyEvent::Back:
    return "back";
  case JoyEvent::Screenshot:
    return "screenshot";
  default:
    return "none";
  }
}

#if defined(CHITRAM_BOARD_S3)

static void joyCalibrateCenter() {
  long sx = 0, sy = 0;
  const int n = 24;
  for (int i = 0; i < n; ++i) {
    sx += analogRead(JOY_VRX_PIN);
    sy += analogRead(JOY_VRY_PIN);
    delay(2);
  }
  sJoyCx = (int)(sx / n);
  sJoyCy = (int)(sy / n);
  inputLog("joy: calibrated center x=%d y=%d", sJoyCx, sJoyCy);
}

static JoyEvent pollHardwareJoystick() {
  static bool dirLatched = false;
  static bool swDown = false;
  static bool swLongSent = false;
  static bool swShotChord = false; // up+click → ignore Ok on release
  static uint32_t swDownMs = 0;

  int x = analogRead(JOY_VRX_PIN);
  int y = analogRead(JOY_VRY_PIN);
  bool swPressed = digitalRead(JOY_SW_PIN) == LOW;
  uint32_t now = millis();

  int dx = x - sJoyCx;
  int dy = y - sJoyCy;
  bool offCenter =
      (dx > JOY_DEADZONE || dx < -JOY_DEADZONE || dy > JOY_DEADZONE ||
       dy < -JOY_DEADZONE);

  auto dominantDir = [&]() -> JoyEvent {
    if (!offCenter) {
      return JoyEvent::None;
    }
    if (abs(dx) >= abs(dy)) {
      return (dx > 0) ? JoyEvent::Right : JoyEvent::Left;
    }
    return (dy > 0) ? JoyEvent::Down : JoyEvent::Up;
  };

  // Stick directions — one event per deflection; must return to center.
  // Skip while SW held (chord handled below).
  if (offCenter && !dirLatched && !swPressed) {
    JoyEvent dir = dominantDir();
    dirLatched = true;
    inputLog("joy hw %s (x=%d y=%d)", joyEventName(dir), x, y);
    return dir;
  }
  if (!offCenter) {
    dirLatched = false;
  }

  // SW down while stick Up → Screenshot; hold → Back; short → Ok
  if (swPressed && !swDown) {
    swDown = true;
    swLongSent = false;
    swShotChord = false;
    swDownMs = now;
    if (dominantDir() == JoyEvent::Up) {
      swShotChord = true;
      inputLog("joy hw screenshot (up+click)");
      return JoyEvent::Screenshot;
    }
  }
  if (swPressed && swDown && !swLongSent && !swShotChord &&
      (now - swDownMs) >= JOY_LONG_MS) {
    swLongSent = true;
    inputLog("joy hw back (hold)");
    return JoyEvent::Back;
  }
  if (!swPressed && swDown) {
    swDown = false;
    if (!swLongSent && !swShotChord) {
      inputLog("joy hw ok");
      return JoyEvent::Ok;
    }
    swShotChord = false;
  }

  return JoyEvent::None;
}

#endif // CHITRAM_BOARD_S3

void inputBegin() {
  joyLine = "";
  rxBytes = 0;
  while (Serial.available()) {
    Serial.read();
  }
#if defined(CHITRAM_BOARD_S3)
  usb_serial_jtag_driver_config_t cfg = {
      .tx_buffer_size = 256,
      .rx_buffer_size = 256,
  };
  esp_err_t err = usb_serial_jtag_driver_install(&cfg);
  s_jtag = (err == ESP_OK);
  inputLog("usb-jtag rx %s", s_jtag ? "ok" : "fail");

  analogReadResolution(12);
  analogSetPinAttenuation(JOY_VRX_PIN, ADC_11db);
  analogSetPinAttenuation(JOY_VRY_PIN, ADC_11db);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  delay(30);
  joyCalibrateCenter();
  inputLog("joy: VRx=GPIO%d VRy=GPIO%d SW=GPIO%d → UI", JOY_VRX_PIN,
           JOY_VRY_PIN, JOY_SW_PIN);
  inputLog("joy: tilt=nav  click=ok  hold=%ums=back  up+click=screenshot",
           (unsigned)JOY_LONG_MS);
#endif
}

static JoyEvent parseJoyCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) {
    return JoyEvent::None;
  }
  if (cmd == "u" || cmd == "up") {
    return JoyEvent::Up;
  }
  if (cmd == "d" || cmd == "down") {
    return JoyEvent::Down;
  }
  if (cmd == "l" || cmd == "left") {
    return JoyEvent::Left;
  }
  if (cmd == "r" || cmd == "right") {
    return JoyEvent::Right;
  }
  if (cmd == "ok" || cmd == "press" || cmd == "enter" || cmd == "a") {
    return JoyEvent::Ok;
  }
  if (cmd == "back" || cmd == "b" || cmd == "esc") {
    return JoyEvent::Back;
  }
  if (cmd == "s" || cmd == "shot" || cmd == "screenshot") {
    return JoyEvent::Screenshot;
  }
  return JoyEvent::None;
}

static JoyEvent takeLine() {
  JoyEvent e = parseJoyCommand(joyLine);
  joyLine = "";
  return e;
}

static JoyEvent feedChar(char c) {
  ++rxBytes;

  if (c == '\r' || c == '\n') {
    if (joyLine.length() == 0) {
      return JoyEvent::None;
    }
    return takeLine();
  }
  if (c < 32) {
    return JoyEvent::None;
  }

  if (joyLine.length() < 24) {
    joyLine += (char)tolower((unsigned char)c);
  }

  if (joyLine.length() == 1) {
    char k = joyLine[0];
    if (k == 'u' || k == 'd' || k == 'l' || k == 'r' || k == 'a' || k == 'b' ||
        k == 's') {
      return takeLine();
    }
  }

  if (joyLine == "ok" || joyLine == "back" || joyLine == "up" ||
      joyLine == "down" || joyLine == "left" || joyLine == "right" ||
      joyLine == "press" || joyLine == "enter" || joyLine == "esc" ||
      joyLine == "shot" || joyLine == "screenshot") {
    JoyEvent e = parseJoyCommand(joyLine);
    joyLine = "";
    return e;
  }
  return JoyEvent::None;
}

JoyEvent inputPoll() {
#if defined(CHITRAM_BOARD_S3)
  JoyEvent hw = pollHardwareJoystick();
  if (hw != JoyEvent::None) {
    return hw;
  }
#endif

  while (Serial.available()) {
    int raw = Serial.read();
    if (raw < 0) {
      break;
    }
    JoyEvent e = feedChar((char)raw);
    if (e != JoyEvent::None) {
      return e;
    }
  }

#if defined(CHITRAM_BOARD_S3)
  if (s_jtag) {
    uint8_t buf[64];
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
    for (int i = 0; i < n; ++i) {
      JoyEvent e = feedChar((char)buf[i]);
      if (e != JoyEvent::None) {
        return e;
      }
    }
  }
#endif

  return JoyEvent::None;
}
