# chitram

ESP32 firmware: Waveshare 2.4" ILI9341 LCD + INMP441 mic → **Deepgram live** speech-to-text → **OpenRouter** image generation on the display.

## Requirements

- [PlatformIO Core](https://platformio.org/install/cli) (`python3 -m platformio`)
- ESP32 DevKit, USB cable
- Waveshare 2.4" LCD (ILI9341 SPI)
- INMP441 I2S MEMS microphone
- WiFi + [Deepgram](https://console.deepgram.com/) API key + [OpenRouter](https://openrouter.ai/) API key

## Secrets

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h` with your WiFi SSID/password, `DEEPGRAM_API_KEY`, and `OPENROUTER_API_KEY`. That file is gitignored.

## Wiring

### LCD (Waveshare 2.4" ILI9341)

| LCD | ESP32 |
|-----|-------|
| VCC | 3.3V |
| GND | GND |
| DIN | GPIO 23 (MOSI) |
| CLK | GPIO 18 (SCK) |
| CS | GPIO 5 |
| DC | GPIO 16 |
| RST | GPIO 17 |
| BL | GPIO 4 |

### Mic (INMP441)

| INMP441 | ESP32 | Notes |
|---------|-------|-------|
| VDD | 3.3V | Never 5V |
| GND | GND | |
| SCK (BCLK) | GPIO 26 | |
| WS (LRCL) | GPIO 25 | |
| SD (DOUT) | GPIO 33 | Data in |
| L/R | GND | Left channel |

### Button (listen / stop toggle)

| Button | ESP32 |
|--------|--------|
| one side | GPIO 27 |
| other side | GND |

Press and **hold** GPIO 27 to talk; release to stop and show the transcript. Serial `listen` / `stop` still work.

## Usage

1. Wire LCD + mic
2. Fill in `include/secrets.h`
3. Flash and open serial:

```bash
python3 -m platformio run --target upload --upload-port /dev/cu.usbserial-0001
python3 -m platformio device monitor
```

4. Wait for **Ready** on the LCD / `commands: listen | stop | help` on serial
5. **Hold the GPIO 27 button** to talk (release to stop), or use serial `listen` / `stop`:
   - hold — stream mic audio to Deepgram live
   - release — finalize and show the transcript
6. Live partial transcripts appear on the LCD while you speak
7. After stop: transcript briefly, then OpenRouter generates a JPEG and draws it full-screen
8. Auto-stops after **5 minutes** if you keep holding

Image gen is memory-optimized: requests **1K / 4:3** from Gemini, stream-decodes base64 **straight into LittleFS** (no large RAM buffer — avoids ESP32 heap fragmentation OOM), then paints with **TJpg_Decoder** / **PNGdec**.

Mic I2S is only on between `listen` and `stop`. Audio is streamed as raw PCM (`linear16` @ 16 kHz) — no multi‑MB RAM buffer.

## Build only

```bash
python3 -m platformio run
```

## Layout

| Path | Role |
|------|------|
| `include/config.h` | Pins + constants |
| `include/display.h` + `src/display.cpp` | ILI9341 UI |
| `include/mic.h` + `src/mic.cpp` | I2S mic, DSP, VU |
| `include/deepgram.h` + `src/deepgram.cpp` | Live STT WebSocket |
| `include/net_services.h` + `src/net_services.cpp` | WiFi + OpenRouter image gen |
| `include/app.h` + `src/app.cpp` | Button/serial flow |
| `src/main.cpp` | `setup()` / `loop()` only |
| `include/secrets.h` | WiFi + API keys (gitignored) |
