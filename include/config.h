#pragma once

// Board selection: CHITRAM_BOARD_S3 from platformio env:esp32s3cam

#if defined(CHITRAM_BOARD_S3)

// Waveshare 2.4" ILI9341 (SPI) ↔ ESP32-S3-WROOM N16R8 CAM
// Avoids camera (4–18), SD (38–40), PSRAM (35–37)
// VCC→3V3 GND→GND DIN→41 CLK→42 CS→21 DC→14 RST→2 BL→1
#define TFT_CS 21
#define TFT_DC 14
#define TFT_RST 2
#define TFT_BL 1
#define TFT_SCLK 42
#define TFT_MOSI 41
#define TFT_SPI_HZ 16000000

// INMP441 (I2S)
// VDD→3V3 GND→GND SCK→47 WS→48 SD→19 L/R→GND
#define I2S_SCK 47
#define I2S_WS 48
#define I2S_SD 19
#define I2S_PORT I2S_NUM_0

// Hold-to-talk / joystick SW: GPIO 20 ↔ GND (internal pull-up)
#define BTN_PIN 20

// Robocraze PS2 joystick → JoyEvent (serial u/d/l/r/a/b still works)
// VCC→3V3 GND→GND VRx→8 VRy→9 SW→20
#define JOY_VRX_PIN 8
#define JOY_VRY_PIN 9
#define JOY_SW_PIN BTN_PIN
#define JOY_DEADZONE 500 // from measured center ~1955/1880
#define JOY_LONG_MS BTN_LONG_MS // hold SW → Back
#define JOY_REPEAT_DELAY_MS 350 // hold before auto-repeat starts
#define JOY_REPEAT_MS 110       // auto-repeat interval while held

// Onboard SD_MMC (do not rewire)
#define SD_MMC_CLK 39
#define SD_MMC_CMD 38
#define SD_MMC_D0 40

#else

// Waveshare 2.4" ILI9341 (SPI) ↔ ESP32 DevKit
// VCC→3V3 GND→GND DIN→23 CLK→18 CS→5 DC→16 RST→17 BL→4
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 17
#define TFT_BL 4
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_SPI_HZ 16000000

// INMP441 (I2S) ↔ ESP32 DevKit
// VDD→3V3 GND→GND SCK→26 WS→25 SD→33 L/R→GND (left)
#define I2S_SCK 26
#define I2S_WS 25
#define I2S_SD 33
#define I2S_PORT I2S_NUM_0

// Momentary button: GPIO 27 ↔ GND (internal pull-up). Hold to talk.
#define BTN_PIN 27

#endif

#define BTN_DEBOUNCE_MS 40
// Tap (< this) toggles listen; hold (≥ this) opens gallery
#define BTN_LONG_MS 800
#define IMAGE_HOLD_MS 8000

#define SAMPLE_RATE 8000
// Small PSRAM rings — Deepgram needs internal RAM for TLS
#define MIC_RING_SECONDS 1
#define MIC_RING_SAMPLES (SAMPLE_RATE * MIC_RING_SECONDS)
// ~100 ms PCM per WS frame at 8 kHz
#define STREAM_CHUNK_SAMPLES 800
#define AUDIO_REC_MAX_SECONDS 15
#define MAX_LISTEN_MS (5UL * 60UL * 1000UL)
// Auto-stop Ask listen after this much with no new speech (post first speech)
#define SILENCE_STOP_MS 2000UL
// Mic peak must reach this to count as sound (AGC targets ~7000)
#define SILENCE_SOUND_PEAK 3500

// Gentler AGC — avoid amplifying hiss in quiet rooms
#define AGC_TARGET_PEAK 7000.0f
#define AGC_GAIN_MIN 1.0f
#define AGC_GAIN_MAX 5.0f
#define AGC_GAIN_SLEW 0.0012f

// Temp: skip WiFi/Deepgram — mic ring → SD WAV only (Ask = record test).
// #define CHITRAM_AUDIO_ONLY 1

#define OR_HOST "openrouter.ai"
#define OR_IMAGE_PATH "/api/v1/images"
#define IMAGE_MODEL "google/gemini-3.1-flash-lite-image"
#define IMAGE_FS_PATH "/gen.bin"
#define B64_WRITE_CHUNK 256

#define DG_HOST "api.deepgram.com"
#define DG_PATH                                                                \
  "/v1/listen?encoding=linear16&sample_rate=8000&channels=1&model=nova-3&"     \
  "language=en&interim_results=true&punctuate=true&smart_format=true&"         \
  "endpointing=1000&utterance_end_ms=2500&vad_events=true"
