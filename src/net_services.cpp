#include "net_services.h"
#include "config.h"
#include "display.h"
#include "deepgram.h"
#include "secrets.h"
#include "settings.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TJpg_Decoder.h>
#include <PNGdec.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "storage.h"
#include "gallery.h"
#include "image_draw.h"
#include "ui_clip.h"

bool connectWifi() {
  settingsBegin();
  WiFi.mode(WIFI_STA);
  WiFi.begin(settingsWifiSsid(), settingsWifiPassword());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERR WiFi failed");
    return false;
  }
  Serial.printf("WiFi OK %s\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(false);
  return true;
}

static bool jpgTftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h,
                         uint16_t *bitmap) {
  if (y >= tft.height() || x >= tft.width()) {
    return false;
  }
  // Adafruit ILI9341 expects native (little-endian) RGB565 via writePixels
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.writePixels(bitmap, (uint32_t)w * (uint32_t)h);
  tft.endWrite();
  return true;
}

static int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

static const char kB64Enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool clientWriteAll(WiFiClientSecure &client, const char *data,
                           size_t len) {
  size_t off = 0;
  uint32_t stallDeadline = millis() + 30000;
  while (off < len) {
    size_t n = client.write((const uint8_t *)data + off, len - off);
    if (n > 0) {
      off += n;
      stallDeadline = millis() + 30000;
      continue;
    }
    if (!client.connected()) {
      return false;
    }
    if (millis() > stallDeadline) {
      return false;
    }
    delay(2);
    yield();
  }
  return true;
}

// Encode file → base64 onto TLS. Writes exactly 4*((size+2)/3) chars.
// Batches ~1KB TLS writes (not 4-byte) so large gallery refs don't stall forever.
static bool streamFileAsBase64(WiFiClientSecure &client, File &f,
                               size_t fileSize) {
  static const size_t kInChunk = 768; // → 1024 base64 chars
  uint8_t inBuf[kInChunk];
  char outBuf[(kInChunk / 3) * 4];
  size_t remaining = fileSize;
  size_t sentRaw = 0;
  uint32_t lastLog = millis();

  while (remaining > 0) {
    size_t want = remaining > kInChunk ? kInChunk : remaining;
    // Keep want multiple of 3 except for final chunk
    if (want < remaining) {
      want -= (want % 3);
      if (want == 0) {
        want = 3;
      }
    }
    size_t got = f.read(inBuf, want);
    if (got != want) {
      Serial.printf("ERR ref read want=%u got=%u\n", (unsigned)want,
                    (unsigned)got);
      return false;
    }
    remaining -= got;
    sentRaw += got;

    size_t outLen = 0;
    size_t i = 0;
    while (i + 3 <= got) {
      uint8_t a = inBuf[i], b = inBuf[i + 1], c = inBuf[i + 2];
      outBuf[outLen++] = kB64Enc[a >> 2];
      outBuf[outLen++] = kB64Enc[((a & 0x03) << 4) | (b >> 4)];
      outBuf[outLen++] = kB64Enc[((b & 0x0F) << 2) | (c >> 6)];
      outBuf[outLen++] = kB64Enc[c & 0x3F];
      i += 3;
    }
    if (i < got) {
      uint8_t a = inBuf[i];
      outBuf[outLen++] = kB64Enc[a >> 2];
      if (i + 1 < got) {
        uint8_t b = inBuf[i + 1];
        outBuf[outLen++] = kB64Enc[((a & 0x03) << 4) | (b >> 4)];
        outBuf[outLen++] = kB64Enc[(b & 0x0F) << 2];
        outBuf[outLen++] = '=';
      } else {
        outBuf[outLen++] = kB64Enc[(a & 0x03) << 4];
        outBuf[outLen++] = '=';
        outBuf[outLen++] = '=';
      }
    }

    if (!clientWriteAll(client, outBuf, outLen)) {
      Serial.printf("ERR b64 write at %u/%u\n", (unsigned)sentRaw,
                    (unsigned)fileSize);
      return false;
    }

    if (millis() - lastLog > 1500) {
      lastLog = millis();
      Serial.printf("edit upload %u/%u\n", (unsigned)sentRaw,
                    (unsigned)fileSize);
    }
    yield();
  }
  Serial.printf("edit upload done %u bytes\n", (unsigned)fileSize);
  return true;
}

struct B64ToFile {
  File *f = nullptr;
  size_t len = 0;
  int sextet[4];
  int n = 0;
  bool done = false;
  bool overflow = false;
  uint8_t wbuf[B64_WRITE_CHUNK];
  size_t wlen = 0;

  bool flush() {
    if (!f || wlen == 0) {
      return true;
    }
    size_t wrote = f->write(wbuf, wlen);
    if (wrote != wlen) {
      overflow = true;
      return false;
    }
    wlen = 0;
    return true;
  }

  bool pushBytes(const uint8_t *p, int count) {
    for (int i = 0; i < count; ++i) {
      if (wlen >= sizeof(wbuf)) {
        if (!flush()) {
          return false;
        }
      }
      wbuf[wlen++] = p[i];
      len++;
    }
    return true;
  }

  bool feed(char ch) {
    if (done || overflow) {
      return !overflow;
    }
    if (ch == '"') {
      done = true;
      return flush();
    }
    if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') {
      return true;
    }
    if (ch == '=') {
      if (n >= 2) {
        uint8_t out[2];
        out[0] = (uint8_t)((sextet[0] << 2) | (sextet[1] >> 4));
        int count = 1;
        if (n > 2) {
          out[1] = (uint8_t)((sextet[1] << 4) | (sextet[2] >> 2));
          count = 2;
        }
        if (!pushBytes(out, count)) {
          return false;
        }
      }
      done = true;
      n = 0;
      return flush();
    }
    int v = b64Value(ch);
    if (v < 0) {
      return true;
    }
    sextet[n++] = v;
    if (n == 4) {
      uint8_t out[3];
      out[0] = (uint8_t)((sextet[0] << 2) | (sextet[1] >> 4));
      out[1] = (uint8_t)((sextet[1] << 4) | (sextet[2] >> 2));
      out[2] = (uint8_t)((sextet[2] << 6) | sextet[3]);
      n = 0;
      return pushBytes(out, 3);
    }
    return true;
  }
};

static bool clientReadChar(WiFiClientSecure &client, char &out, uint32_t deadline) {
  while (millis() < deadline) {
    if (client.available()) {
      int v = client.read();
      if (v < 0) {
        return false;
      }
      out = (char)v;
      return true;
    }
    if (!client.connected() && !client.available()) {
      return false;
    }
    delay(1);
    yield();
  }
  return false;
}

static bool skipHttpHeaders(WiFiClientSecure &client, bool &chunked,
                            int &contentLen, uint32_t deadline) {
  chunked = false;
  contentLen = -1;
  String line;
  line.reserve(96);
  while (millis() < deadline) {
    line = "";
    while (millis() < deadline) {
      char c;
      if (!clientReadChar(client, c, deadline)) {
        return false;
      }
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        break;
      }
      if (line.length() < 120) {
        line += c;
      }
    }
    if (line.length() == 0) {
      return true;  // end of headers
    }
    line.toLowerCase();
    if (line.startsWith("transfer-encoding:") && line.indexOf("chunked") >= 0) {
      chunked = true;
    }
    if (line.startsWith("content-length:")) {
      contentLen = line.substring(15).toInt();
    }
  }
  return false;
}

static bool matchNeedleFeed(char c, const char *needle, size_t nlen, size_t &m) {
  if (c == needle[m]) {
    m++;
    if (m == nlen) {
      m = 0;
      return true;
    }
  } else if (c == needle[0]) {
    m = 1;
  } else {
    m = 0;
  }
  return false;
}

static bool streamDecodeB64FieldToFile(WiFiClientSecure &client, bool chunked,
                                       int contentLen, B64ToFile &dec,
                                       uint32_t deadline) {
  const char *needle = "\"b64_json\":\"";
  const size_t nlen = 12;
  size_t match = 0;
  bool inValue = false;

  auto consume = [&](char c) -> bool {
    if (!inValue) {
      if (matchNeedleFeed(c, needle, nlen, match)) {
        inValue = true;
      }
      return true;
    }
    return dec.feed(c);
  };

  if (chunked) {
    while (millis() < deadline && !dec.done && !dec.overflow) {
      String sizeLine;
      while (millis() < deadline) {
        char c;
        if (!clientReadChar(client, c, deadline)) {
          return dec.len > 0;
        }
        if (c == '\r') {
          continue;
        }
        if (c == '\n') {
          break;
        }
        sizeLine += c;
      }
      long chunk = strtol(sizeLine.c_str(), nullptr, 16);
      if (chunk <= 0) {
        break;
      }
      for (long i = 0; i < chunk; ++i) {
        char c;
        if (!clientReadChar(client, c, deadline)) {
          return false;
        }
        if (!consume(c)) {
          return false;
        }
        if (dec.done) {
          return dec.flush() && dec.len > 0;
        }
      }
      char drop;
      clientReadChar(client, drop, deadline);
      if (drop == '\r') {
        clientReadChar(client, drop, deadline);
      }
    }
  } else {
    int remaining = contentLen;
    while (millis() < deadline && !dec.done && !dec.overflow) {
      if (contentLen >= 0 && remaining == 0) {
        break;
      }
      char c;
      if (!clientReadChar(client, c, deadline)) {
        break;
      }
      if (remaining > 0) {
        remaining--;
      }
      if (!consume(c)) {
        return false;
      }
      if (dec.done) {
        return dec.flush() && dec.len > 0;
      }
    }
  }
  return dec.flush() && dec.len > 0 && !dec.overflow;
}

static bool drawJpegFromFs(const char *path) {
  reclaimDisplay();
  // false: TJpg outputs host-endian RGB565; Adafruit writePixels expects that.
  // (true + drawRGBBitmap was double-swapping → neon/wrong colors)
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpgTftOutput);

  uint16_t jw = 0, jh = 0;
  if (TJpgDec.getFsJpgSize(&jw, &jh, path, imageFs()) != JDR_OK || jw == 0) {
    Serial.println("ERR jpeg header (fs)");
    return false;
  }
  Serial.printf("jpeg %ux%u heap=%u\n", jw, jh, (unsigned)ESP.getFreeHeap());

  uint8_t scale = 1;
  while (scale < 8 && (jw / scale > (uint16_t)tft.width() ||
                       jh / scale > (uint16_t)tft.height())) {
    scale = (uint8_t)(scale * 2);
  }
  TJpgDec.setJpgScale(scale);

  uint16_t dw = jw / scale;
  uint16_t dh = jh / scale;
  int16_t x = (int16_t)((tft.width() - (int)dw) / 2);
  int16_t y = (int16_t)((tft.height() - (int)dh) / 2);
  if (x < 0) {
    x = 0;
  }
  if (y < 0) {
    y = 0;
  }

  tft.fillScreen(ILI9341_BLACK);
  JRESULT jr = TJpgDec.drawFsJpg(x, y, path, imageFs());
  if (jr != JDR_OK) {
    Serial.printf("ERR drawFsJpg %d\n", (int)jr);
    return false;
  }
  return true;
}

static PNG pngDec;
static int pngScale = 4;
static int16_t pngOffX = 0;
static int16_t pngOffY = 0;
static File pngFile;

static void *pngOpen(const char *filename, int32_t *size) {
  pngFile = imageFs().open(filename, FILE_READ);
  if (!pngFile) {
    return nullptr;
  }
  *size = (int32_t)pngFile.size();
  return &pngFile;
}

static void pngClose(void *handle) {
  (void)handle;
  if (pngFile) {
    pngFile.close();
  }
}

static int32_t pngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  (void)handle;
  return (int32_t)pngFile.read(buffer, length);
}

static int32_t pngSeek(PNGFILE *handle, int32_t position) {
  (void)handle;
  return pngFile.seek(position) ? position : -1;
}

static int pngDrawLine(PNGDRAW *pDraw) {
  if ((pDraw->y % pngScale) != 0) {
    return 1;
  }
  int dy = pngOffY + (pDraw->y / pngScale);
  if (dy < 0 || dy >= tft.height()) {
    return 1;
  }
  uint16_t lineBuf[1024];
  if (pDraw->iWidth > 1024) {
    return 1;
  }
  pngDec.getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  int dw = pDraw->iWidth / pngScale;
  if (dw > tft.width()) {
    dw = tft.width();
  }
  uint16_t out[320];
  for (int x = 0; x < dw; ++x) {
    out[x] = lineBuf[x * pngScale];
  }
  tft.startWrite();
  tft.setAddrWindow(pngOffX, dy, dw, 1);
  tft.writePixels(out, (uint32_t)dw);
  tft.endWrite();
  return 1;
}

static bool drawPngFromFs(const char *path) {
  reclaimDisplay();
  int rc = pngDec.open(path, pngOpen, pngClose, pngRead, pngSeek, pngDrawLine);
  if (rc != PNG_SUCCESS) {
    Serial.printf("ERR png open %d\n", rc);
    return false;
  }
  int pw = pngDec.getWidth();
  int ph = pngDec.getHeight();
  Serial.printf("png %dx%d heap=%u\n", pw, ph, (unsigned)ESP.getFreeHeap());

  pngScale = 1;
  while (pngScale < 8 &&
         (pw / pngScale > tft.width() || ph / pngScale > tft.height())) {
    pngScale *= 2;
  }
  int dw = pw / pngScale;
  int dh = ph / pngScale;
  pngOffX = (tft.width() - dw) / 2;
  pngOffY = (tft.height() - dh) / 2;
  if (pngOffX < 0) {
    pngOffX = 0;
  }
  if (pngOffY < 0) {
    pngOffY = 0;
  }

  tft.fillScreen(ILI9341_BLACK);
  rc = pngDec.decode(nullptr, 0);
  pngDec.close();
  if (rc != PNG_SUCCESS) {
    Serial.printf("ERR png decode %d\n", rc);
    return false;
  }
  return true;
}

// POST /api/v1/images → stream b64 to FS → decode to LCD (no big RAM buf)
bool generateAndShowImage(const String &promptIn, char *outPath, size_t outLen,
                          bool draw, const char *referencePath) {
  if (outPath && outLen) {
    outPath[0] = '\0';
  }
  if (String(OPENROUTER_API_KEY).length() < 8 ||
      String(OPENROUTER_API_KEY) == "REPLACE_ME" ||
      String(OPENROUTER_API_KEY).startsWith("sk-or-v1-...")) {
    Serial.println("ERR set OPENROUTER_API_KEY in secrets.h");
    return false;
  }

  char prompt[224];
  {
    String tmp = promptIn;
    tmp.trim();
    const char *aiPrefix = settingsAiPrompt();
    if (aiPrefix && aiPrefix[0]) {
      tmp = String(aiPrefix) + ", " + tmp;
    }
    if (tmp.length() > 220) {
      tmp = tmp.substring(0, 220);
    }
    if (tmp.length() < 2) {
      return false;
    }
    tmp.replace("\\", "\\\\");
    tmp.replace("\"", "\\\"");
    tmp.replace("\n", " ");
    tmp.replace("\r", " ");
    strncpy(prompt, tmp.c_str(), sizeof(prompt) - 1);
    prompt[sizeof(prompt) - 1] = '\0';
  }

  // Drop large STT strings before TLS — reduces fragmentation pressure
  deepgramClearText();
  displayResetTranscriptCache();

  const char *model = settingsImageModel();
  if (!model || !model[0]) {
    model = IMAGE_MODEL;
  }

  const bool editing = referencePath && referencePath[0];
  File refFile;
  size_t refSize = 0;
  bool refJpeg = true;
  if (editing) {
    if (!storageBegin()) {
      Serial.println("ERR image FS mount failed");
      return false;
    }
    refFile = imageFs().open(referencePath, FILE_READ);
    if (!refFile || refFile.size() < 4) {
      Serial.printf("ERR reference open %s\n", referencePath);
      if (refFile) {
        refFile.close();
      }
      return false;
    }
    refSize = refFile.size();
    uint8_t magic[4] = {0};
    refFile.read(magic, 4);
    refFile.seek(0);
    refJpeg = magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF;
    bool refPng =
        magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
    if (!refJpeg && !refPng) {
      Serial.println("ERR reference not jpeg/png");
      refFile.close();
      return false;
    }
  }

  // Prompt: illustration prefix for text-to-image; raw prompt when editing
  String promptField =
      editing ? String(prompt) : (String("Illustration of: ") + prompt);

  const char *aspect = settingsAspectRatio();
  if (!aspect || !aspect[0]) {
    aspect = "4:3";
  }
  const char *resolution = settingsResolution();
  if (!resolution || !resolution[0]) {
    resolution = "1K";
  }

  // Prefer JPEG: PNGdec can't open typical GPT PNGs (INVALID_FILE / TOO_BIG).
  // OpenRouter: output_format png|jpeg|webp; output_compression 0–100 for jpeg/webp.
  String bodyPrefix = String("{\"model\":\"") + model + "\",\"prompt\":\"" +
                      promptField + "\",\"n\":1,\"aspect_ratio\":\"" + aspect +
                      "\",\"resolution\":\"" + resolution +
                      "\",\"output_format\":\"jpeg\",\"output_compression\":80";
  String bodySuffix;
  size_t contentLen = 0;
  size_t b64Len = 0;

  if (editing) {
    const char *mime = refJpeg ? "image/jpeg" : "image/png";
    bodyPrefix +=
        String(",\"input_references\":[{\"type\":\"image_url\",\"image_url\":{"
               "\"url\":\"data:") +
        mime + ";base64,";
    bodySuffix = "\"}}]}";
    b64Len = 4 * ((refSize + 2) / 3);
    contentLen = bodyPrefix.length() + b64Len + bodySuffix.length();
  } else {
    bodyPrefix += "}";
    contentLen = bodyPrefix.length();
  }

  Serial.printf(
      "image gen model=%s aspect=%s res=%s edit=%d ref=%u body=%u heap=%u\n",
      model, aspect, resolution, (int)editing, (unsigned)refSize,
      (unsigned)contentLen, (unsigned)ESP.getFreeHeap());

  if (!storageBegin()) {
    if (refFile) {
      refFile.close();
    }
    Serial.println("ERR image FS mount failed");
    return false;
  }

  // Important on SD_MMC: only one file open at a time.
  // Send the request (streaming ref) first, then open /gen.bin for the reply.
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30);
  client.setTimeout(120);

  reclaimDisplay();

  Serial.println("image gen: TLS connect...");
  if (!client.connect(OR_HOST, 443, 20000)) {
    Serial.println("ERR OpenRouter TLS failed");
    if (refFile) {
      refFile.close();
    }
    return false;
  }
  Serial.println("image gen: TLS ok, sending body...");

  client.print(String("POST ") + OR_IMAGE_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + OR_HOST + "\r\n");
  client.print(String("Authorization: Bearer ") + OPENROUTER_API_KEY + "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("HTTP-Referer: https://github.com/chitram\r\n");
  client.print("X-Title: chitram\r\n");
  client.print(String("Content-Length: ") + contentLen + "\r\n");
  client.print("Connection: close\r\n\r\n");

  if (!clientWriteAll(client, bodyPrefix.c_str(), bodyPrefix.length())) {
    Serial.println("ERR body prefix write");
    if (refFile) {
      refFile.close();
    }
    client.stop();
    return false;
  }
  bodyPrefix = "";
  promptField = "";

  if (editing) {
    if (!streamFileAsBase64(client, refFile, refSize)) {
      Serial.println("ERR reference base64 stream");
      refFile.close();
      client.stop();
      return false;
    }
    refFile.close();
    if (!clientWriteAll(client, bodySuffix.c_str(), bodySuffix.length())) {
      Serial.println("ERR body suffix write");
      client.stop();
      return false;
    }
    bodySuffix = "";
  }

  // Now open output file for the response (ref already closed)
  imageFs().remove(IMAGE_FS_PATH);
  File out = imageFs().open(IMAGE_FS_PATH, FILE_WRITE);
  if (!out) {
    Serial.println("ERR cannot create image file");
    client.stop();
    return false;
  }

  // Upload can take a while; allow extra time for the model reply
  uint32_t deadline = millis() + 180000;
  Serial.println("image gen: waiting for response...");

  String status;
  while (millis() < deadline) {
    char c;
    if (!clientReadChar(client, c, deadline)) {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      break;
    }
    if (status.length() < 80) {
      status += c;
    }
  }
  Serial.println(status);
  if (status.indexOf("200") < 0 && status.indexOf("201") < 0) {
    String err;
    while (millis() < deadline && err.length() < 220) {
      char c;
      if (!clientReadChar(client, c, deadline)) {
        break;
      }
      err += c;
    }
    Serial.println(err);
    client.stop();
    out.close();
    imageFs().remove(IMAGE_FS_PATH);
    return false;
  }

  bool chunked = false;
  int contentLenHdr = -1;
  if (!skipHttpHeaders(client, chunked, contentLenHdr, deadline)) {
    Serial.println("ERR headers");
    client.stop();
    out.close();
    imageFs().remove(IMAGE_FS_PATH);
    return false;
  }

  B64ToFile dec;
  dec.f = &out;
  if (!streamDecodeB64FieldToFile(client, chunked, contentLenHdr, dec,
                                  deadline)) {
    Serial.printf("ERR b64→fs len=%u overflow=%d\n", (unsigned)dec.len,
                  (int)dec.overflow);
    client.stop();
    out.close();
    imageFs().remove(IMAGE_FS_PATH);
    return false;
  }
  client.stop();
  out.close();

  Serial.printf("image file %u bytes heap=%u maxAlloc=%u\n", (unsigned)dec.len,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  File peek = imageFs().open(IMAGE_FS_PATH, FILE_READ);
  if (!peek || peek.size() < 4) {
    Serial.println("ERR empty image file");
    if (peek) {
      peek.close();
    }
    imageFs().remove(IMAGE_FS_PATH);
    return false;
  }
  uint8_t magic[4] = {0};
  peek.read(magic, 4);
  peek.close();

  bool isJpeg = magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF;
  bool isPng =
      magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
  Serial.printf("magic jpeg=%d png=%d\n", (int)isJpeg, (int)isPng);

  if (!isJpeg && !isPng) {
    Serial.printf("ERR unknown magic %02X %02X %02X %02X\n", magic[0], magic[1],
                  magic[2], magic[3]);
    imageFs().remove(IMAGE_FS_PATH);
    return false;
  }

  char saved[40];
  if (!gallerySaveFromTemp(IMAGE_FS_PATH, isJpeg, saved, sizeof(saved))) {
    if (outPath && outLen) {
      strncpy(outPath, IMAGE_FS_PATH, outLen - 1);
      outPath[outLen - 1] = '\0';
    }
    if (!draw) {
      return true;
    }
    bool ok =
        isJpeg ? drawJpegFromFs(IMAGE_FS_PATH) : drawPngFromFs(IMAGE_FS_PATH);
    imageFs().remove(IMAGE_FS_PATH);
    if (outPath && outLen) {
      outPath[0] = '\0';
    }
    return ok;
  }

  if (outPath && outLen) {
    strncpy(outPath, saved, outLen - 1);
    outPath[outLen - 1] = '\0';
  }
  if (!draw) {
    return true;
  }
  return drawImageFile(saved);
}

// --- Scale image into RGB565 buffer (gallery thumbs + fullscreen viewer) ---
// Panel-sized shared buffer for 1× / thumbs.
static const int kThumbMaxW = 320;
static const int kThumbMaxH = 240;
// Zoom canvas may be up to 4× panel (still far smaller than a 1K/2K frame).
static const int kRenderMaxW = 320 * 4;
static const int kRenderMaxH = 240 * 4;
static uint16_t *thumbBuf = nullptr;
static int thumbBufCap = 0; // pixels
static uint16_t *thumbOut = nullptr;
static int thumbDw = 0;
static int thumbDh = 0;
static int thumbSrcW = 0;
static int thumbSrcH = 0;
// Source crop window (cover mode). Full frame when letterboxing.
static int thumbSampleX0 = 0;
static int thumbSampleY0 = 0;
static int thumbSampleW = 0;
static int thumbSampleH = 0;
// Zoom/pan for legacy crop-during-decode fallback.
static int thumbZoom = 1;
static float thumbPanX = 0.5f;
static float thumbPanY = 0.5f;

// PSRAM cover canvas for zoom≥2 — decode once, pan by blit.
static char sZoomPath[48] = {};
static int sZoomLevel = 0;
static uint16_t *sZoomPx = nullptr;
static int sZoomW = 0;
static int sZoomH = 0;
static int sZoomCap = 0;

static bool ensureThumbBuf(int pixels) {
  if (pixels < 1) {
    return false;
  }
  if (thumbBuf && thumbBufCap >= pixels) {
    return true;
  }
  if (thumbBuf) {
    free(thumbBuf);
    thumbBuf = nullptr;
    thumbBufCap = 0;
  }
  size_t bytes = (size_t)pixels * sizeof(uint16_t);
  thumbBuf = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM |
                                                     MALLOC_CAP_8BIT);
  if (!thumbBuf) {
    thumbBuf = (uint16_t *)malloc(bytes);
  }
  if (!thumbBuf) {
    Serial.printf("ERR thumb buf %u bytes\n", (unsigned)bytes);
    return false;
  }
  thumbBufCap = pixels;
  return true;
}

static bool thumbJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h,
                           uint16_t *bitmap) {
  if (thumbSampleW <= 0 || thumbSampleH <= 0 || thumbDw <= 0 || thumbDh <= 0) {
    return false;
  }
  for (uint16_t row = 0; row < h; ++row) {
    int sy = y + (int)row;
    if (sy < thumbSampleY0 || sy >= thumbSampleY0 + thumbSampleH) {
      continue;
    }
    // Map source row to a dest y-span so upscaling never leaves black gaps.
    int relY = sy - thumbSampleY0;
    int dy0 = relY * thumbDh / thumbSampleH;
    int dy1 = (relY + 1) * thumbDh / thumbSampleH;
    if (dy1 <= dy0) {
      dy1 = dy0 + 1;
    }
    if (dy0 < 0) {
      dy0 = 0;
    }
    if (dy1 > thumbDh) {
      dy1 = thumbDh;
    }
    if (dy0 >= dy1) {
      continue;
    }
    uint16_t *src = bitmap + (size_t)row * w;
    for (uint16_t col = 0; col < w; ++col) {
      int sx = x + (int)col;
      if (sx < thumbSampleX0 || sx >= thumbSampleX0 + thumbSampleW) {
        continue;
      }
      int relX = sx - thumbSampleX0;
      int dx0 = relX * thumbDw / thumbSampleW;
      int dx1 = (relX + 1) * thumbDw / thumbSampleW;
      if (dx1 <= dx0) {
        dx1 = dx0 + 1;
      }
      if (dx0 < 0) {
        dx0 = 0;
      }
      if (dx1 > thumbDw) {
        dx1 = thumbDw;
      }
      uint16_t px = src[col];
      for (int dy = dy0; dy < dy1; ++dy) {
        uint16_t *dst = thumbOut + (size_t)dy * (size_t)thumbDw;
        for (int dx = dx0; dx < dx1; ++dx) {
          dst[dx] = px;
        }
      }
    }
  }
  return true;
}

static int thumbPngScale = 8;

static int pngThumbDrawLine(PNGDRAW *pDraw) {
  if (thumbSampleW <= 0 || thumbDh <= 0 || thumbDw <= 0) {
    return 1;
  }
  if ((pDraw->y % thumbPngScale) != 0) {
    return 1;
  }
  int sy = pDraw->y / thumbPngScale;
  if (sy < thumbSampleY0 || sy >= thumbSampleY0 + thumbSampleH) {
    return 1;
  }
  int relY = sy - thumbSampleY0;
  int dy0 = relY * thumbDh / thumbSampleH;
  int dy1 = (relY + 1) * thumbDh / thumbSampleH;
  if (dy1 <= dy0) {
    dy1 = dy0 + 1;
  }
  if (dy0 < 0) {
    dy0 = 0;
  }
  if (dy1 > thumbDh) {
    dy1 = thumbDh;
  }
  if (dy0 >= dy1) {
    return 1;
  }
  uint16_t lineBuf[1024];
  if (pDraw->iWidth > 1024) {
    return 1;
  }
  pngDec.getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  int sw = pDraw->iWidth / thumbPngScale;
  for (int x = 0; x < sw; ++x) {
    if (x < thumbSampleX0 || x >= thumbSampleX0 + thumbSampleW) {
      continue;
    }
    int relX = x - thumbSampleX0;
    int dx0 = relX * thumbDw / thumbSampleW;
    int dx1 = (relX + 1) * thumbDw / thumbSampleW;
    if (dx1 <= dx0) {
      dx1 = dx0 + 1;
    }
    if (dx0 < 0) {
      dx0 = 0;
    }
    if (dx1 > thumbDw) {
      dx1 = thumbDw;
    }
    uint16_t px = lineBuf[x * thumbPngScale];
    for (int dy = dy0; dy < dy1; ++dy) {
      uint16_t *dst = thumbOut + (size_t)dy * (size_t)thumbDw;
      for (int dx = dx0; dx < dx1; ++dx) {
        dst[dx] = px;
      }
    }
  }
  return 1;
}

static void thumbSetSampleWindow(int srcW, int srcH, int dstW, int dstH,
                                 bool cover) {
  thumbSampleX0 = 0;
  thumbSampleY0 = 0;
  thumbSampleW = srcW;
  thumbSampleH = srcH;
  if (!cover || srcW < 1 || srcH < 1 || dstW < 1 || dstH < 1) {
    return;
  }
  // Cover: crop the larger axis so aspect matches dest.
  if ((int32_t)srcW * dstH > (int32_t)srcH * dstW) {
    // Source is wider — crop left/right
    thumbSampleH = srcH;
    thumbSampleW = (int)((int32_t)srcH * dstW / dstH);
    if (thumbSampleW < 1) {
      thumbSampleW = 1;
    }
    if (thumbSampleW > srcW) {
      thumbSampleW = srcW;
    }
    thumbSampleX0 = (srcW - thumbSampleW) / 2;
  } else {
    // Source is taller — crop top/bottom
    thumbSampleW = srcW;
    thumbSampleH = (int)((int32_t)srcW * dstH / dstW);
    if (thumbSampleH < 1) {
      thumbSampleH = 1;
    }
    if (thumbSampleH > srcH) {
      thumbSampleH = srcH;
    }
    thumbSampleY0 = (srcH - thumbSampleH) / 2;
  }

  // Zoom: shrink the cover window and offset by pan (still only fills dst buffer).
  if (thumbZoom > 1) {
    const int baseX = thumbSampleX0;
    const int baseY = thumbSampleY0;
    const int baseW = thumbSampleW;
    const int baseH = thumbSampleH;
    int sw = baseW / thumbZoom;
    int sh = baseH / thumbZoom;
    if (sw < 1) {
      sw = 1;
    }
    if (sh < 1) {
      sh = 1;
    }
    float px = thumbPanX;
    float py = thumbPanY;
    if (px < 0.f) {
      px = 0.f;
    }
    if (px > 1.f) {
      px = 1.f;
    }
    if (py < 0.f) {
      py = 0.f;
    }
    if (py > 1.f) {
      py = 1.f;
    }
    const int maxOX = baseW - sw;
    const int maxOY = baseH - sh;
    thumbSampleX0 = baseX + (int)(px * (float)maxOX + 0.5f);
    thumbSampleY0 = baseY + (int)(py * (float)maxOY + 0.5f);
    thumbSampleW = sw;
    thumbSampleH = sh;
  }
}

static void thumbClampScale(uint16_t srcW, uint16_t srcH, int maxW, int maxH,
                            uint8_t *scaleInOut) {
  // Never decode below dest size — forward mapping would leave black gaps.
  uint8_t scale = *scaleInOut;
  while (scale > 1 &&
         ((int)(srcW / scale) < maxW || (int)(srcH / scale) < maxH)) {
    scale = (uint8_t)(scale / 2);
  }
  *scaleInOut = scale;
}

static bool renderToThumb(const char *path, int maxW, int maxH, bool isJpeg,
                          bool cover) {
  if (maxW > kRenderMaxW) {
    maxW = kRenderMaxW;
  }
  if (maxH > kRenderMaxH) {
    maxH = kRenderMaxH;
  }
  if (maxW < 8 || maxH < 8) {
    return false;
  }
  // Caller-supplied buffer (gallery thumbs / zoom canvas) skips shared alloc.
  if (!thumbOut || thumbOut == thumbBuf) {
    if (!ensureThumbBuf(maxW * maxH)) {
      return false;
    }
    thumbOut = thumbBuf;
  }

  // When zoomed, decode sharper source so the crop still fills the panel.
  // Output buffer stays maxW×maxH — zoom never allocates a full-res frame.
  const int z = (cover && thumbZoom > 1) ? thumbZoom : 1;
  const int needW = maxW * z;
  const int needH = maxH * z;
  // Large canvases (zoom cache): stop near ~1× dest for speed.
  // Panel-sized draws keep a sharper ~1.5–2× source.
  const bool largeCanvas = (maxW * maxH) > (kThumbMaxW * kThumbMaxH);

  if (isJpeg) {
    TJpgDec.setSwapBytes(false);
    uint16_t jw = 0, jh = 0;
    if (TJpgDec.getFsJpgSize(&jw, &jh, path, imageFs()) != JDR_OK || jw == 0) {
      return false;
    }
    uint8_t scale = 1;
    while (scale < 8) {
      uint8_t next = (uint8_t)(scale * 2);
      if ((int)(jw / next) < needW || (int)(jh / next) < needH) {
        break;
      }
      if (largeCanvas) {
        if (jw / scale <= (uint16_t)needW && jh / scale <= (uint16_t)needH) {
          break;
        }
      } else if (jw / scale <= (uint16_t)(needW * 2) &&
                 jh / scale <= (uint16_t)(needH * 2)) {
        break;
      }
      scale = next;
    }
    if (cover && !largeCanvas) {
      while (scale < 8) {
        uint8_t next = (uint8_t)(scale * 2);
        if ((int)(jw / next) < needW || (int)(jh / next) < needH) {
          break;
        }
        if (jw / scale <= (uint16_t)(needW * 3 / 2) ||
            jh / scale <= (uint16_t)(needH * 3 / 2)) {
          break;
        }
        scale = next;
      }
    } else if (!cover) {
      while (scale < 8) {
        uint8_t next = (uint8_t)(scale * 2);
        if ((int)(jw / next) < needW || (int)(jh / next) < needH) {
          break;
        }
        if (jw / scale <= (uint16_t)needW && jh / scale <= (uint16_t)needH) {
          break;
        }
        scale = next;
      }
    }
    // Large canvas: allow mild upsample rather than forcing full-res decode.
    if (largeCanvas) {
      while (scale > 1 &&
             ((int)(jw / scale) < needW * 3 / 4 ||
              (int)(jh / scale) < needH * 3 / 4)) {
        scale = (uint8_t)(scale / 2);
      }
    } else {
      thumbClampScale(jw, jh, needW, needH, &scale);
    }
    TJpgDec.setJpgScale(scale);
    thumbSrcW = (int)(jw / scale);
    thumbSrcH = (int)(jh / scale);
    if (thumbSrcW < 1 || thumbSrcH < 1) {
      return false;
    }
    if (cover) {
      thumbDw = maxW;
      thumbDh = maxH;
    } else if (thumbSrcW * maxH > thumbSrcH * maxW) {
      thumbDw = maxW;
      thumbDh = (int)((int32_t)thumbSrcH * maxW / thumbSrcW);
    } else {
      thumbDh = maxH;
      thumbDw = (int)((int32_t)thumbSrcW * maxH / thumbSrcH);
    }
    if (thumbDw < 1) {
      thumbDw = 1;
    }
    if (thumbDh < 1) {
      thumbDh = 1;
    }
    thumbSetSampleWindow(thumbSrcW, thumbSrcH, thumbDw, thumbDh, cover);
    memset(thumbOut, 0, (size_t)thumbDw * (size_t)thumbDh * sizeof(uint16_t));
    TJpgDec.setCallback(thumbJpgOutput);
    return TJpgDec.drawFsJpg(0, 0, path, imageFs()) == JDR_OK;
  }

  // PNG
  int rc = pngDec.open(path, pngOpen, pngClose, pngRead, pngSeek, pngThumbDrawLine);
  if (rc != PNG_SUCCESS) {
    return false;
  }
  int pw = pngDec.getWidth();
  int ph = pngDec.getHeight();
  thumbPngScale = 1;
  while (thumbPngScale < 8) {
    int next = thumbPngScale * 2;
    if (pw / next < needW || ph / next < needH) {
      break;
    }
    if (pw / thumbPngScale <= needW * 2 && ph / thumbPngScale <= needH * 2) {
      break;
    }
    thumbPngScale = next;
  }
  if (cover) {
    while (thumbPngScale < 8) {
      int next = thumbPngScale * 2;
      if (pw / next < needW || ph / next < needH) {
        break;
      }
      if (pw / thumbPngScale <= needW * 3 / 2 ||
          ph / thumbPngScale <= needH * 3 / 2) {
        break;
      }
      thumbPngScale = next;
    }
  } else {
    while (thumbPngScale < 8) {
      int next = thumbPngScale * 2;
      if (pw / next < needW || ph / next < needH) {
        break;
      }
      if (pw / thumbPngScale <= needW && ph / thumbPngScale <= needH) {
        break;
      }
      thumbPngScale = next;
    }
  }
  while (thumbPngScale > 1 &&
         (pw / thumbPngScale < needW || ph / thumbPngScale < needH)) {
    thumbPngScale /= 2;
  }
  thumbSrcW = pw / thumbPngScale;
  thumbSrcH = ph / thumbPngScale;
  if (thumbSrcW < 1 || thumbSrcH < 1) {
    pngDec.close();
    return false;
  }
  if (cover) {
    thumbDw = maxW;
    thumbDh = maxH;
  } else if (thumbSrcW * maxH > thumbSrcH * maxW) {
    thumbDw = maxW;
    thumbDh = (int)((int32_t)thumbSrcH * maxW / thumbSrcW);
  } else {
    thumbDh = maxH;
    thumbDw = (int)((int32_t)thumbSrcW * maxH / thumbSrcH);
  }
  if (thumbDw < 1) {
    thumbDw = 1;
  }
  if (thumbDh < 1) {
    thumbDh = 1;
  }
  thumbSetSampleWindow(thumbSrcW, thumbSrcH, thumbDw, thumbDh, cover);
  memset(thumbOut, 0, (size_t)thumbDw * (size_t)thumbDh * sizeof(uint16_t));
  rc = pngDec.decode(nullptr, 0);
  pngDec.close();
  return rc == PNG_SUCCESS;
}

static bool peekImageType(const char *path, bool *isJpeg, bool *isPng) {
  *isJpeg = false;
  *isPng = false;
  if (!path || !storageBegin()) {
    return false;
  }
  File peek = imageFs().open(path, FILE_READ);
  if (!peek || peek.size() < 4) {
    if (peek) {
      peek.close();
    }
    return false;
  }
  uint8_t magic[4] = {0};
  peek.read(magic, 4);
  peek.close();
  *isJpeg = magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF;
  *isPng =
      magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
  return *isJpeg || *isPng;
}

void blitRgb565(int16_t x, int16_t y, int16_t w, int16_t h,
                const uint16_t *rgb565) {
  if (!rgb565 || w < 1 || h < 1) {
    return;
  }
  int16_t y0, y1;
  if (!uiClipSpan(y, h, &y0, &y1)) {
    return;
  }
  reclaimDisplay();
  const int row0 = y0 - y;
  const int rows = y1 - y0;
  tft.startWrite();
  tft.setAddrWindow(x, y0, (uint16_t)w, (uint16_t)rows);
  tft.writePixels(const_cast<uint16_t *>(rgb565 + (size_t)row0 * (size_t)w),
                  (uint32_t)w * (uint32_t)rows);
  tft.endWrite();
}

// Blit a w×h window from a larger RGB565 bitmap (srcStride pixels/row).
static void blitRgb565Region(int16_t dx, int16_t dy, int16_t w, int16_t h,
                             const uint16_t *src, int srcStride, int sx,
                             int sy) {
  if (!src || w < 1 || h < 1 || srcStride < w) {
    return;
  }
  reclaimDisplay();
  tft.startWrite();
  tft.setAddrWindow(dx, dy, (uint16_t)w, (uint16_t)h);
  for (int row = 0; row < h; ++row) {
    const uint16_t *line =
        src + (size_t)(sy + row) * (size_t)srcStride + (size_t)sx;
    tft.writePixels(const_cast<uint16_t *>(line), (uint32_t)w);
  }
  tft.endWrite();
}

void imageZoomCacheClear() {
  if (sZoomPx) {
    free(sZoomPx);
    sZoomPx = nullptr;
  }
  sZoomCap = 0;
  sZoomW = 0;
  sZoomH = 0;
  sZoomLevel = 0;
  sZoomPath[0] = '\0';
}

// Build a 2× cover canvas once per image. 2× pans blit from it; 4× pans
// nearest-neighbor upscale a half-size window (no second SD decode).
static bool ensureZoomCanvas(const char *path, int sw, int sh) {
  const int cw = sw * 2;
  const int ch = sh * 2;
  if (sZoomPx && sZoomLevel == 2 && sZoomW == cw && sZoomH == ch &&
      sZoomPath[0] && strcmp(sZoomPath, path) == 0) {
    return true;
  }

  const int need = cw * ch;
  if (!sZoomPx || sZoomCap < need) {
    if (sZoomPx) {
      free(sZoomPx);
      sZoomPx = nullptr;
      sZoomCap = 0;
    }
    size_t bytes = (size_t)need * sizeof(uint16_t);
    sZoomPx = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM |
                                                       MALLOC_CAP_8BIT);
    if (!sZoomPx) {
      sZoomPx = (uint16_t *)malloc(bytes);
    }
    if (!sZoomPx) {
      Serial.printf("ERR zoom canvas %u bytes\n", (unsigned)bytes);
      return false;
    }
    sZoomCap = need;
  }

  bool isJpeg = false, isPng = false;
  if (!peekImageType(path, &isJpeg, &isPng)) {
    return false;
  }

  const int savedZoom = thumbZoom;
  thumbZoom = 1;
  thumbOut = sZoomPx;
  bool ok = renderToThumb(path, cw, ch, isJpeg, true);
  thumbOut = thumbBuf;
  thumbZoom = savedZoom;
  if (!ok || thumbDw != cw || thumbDh != ch) {
    Serial.printf("ERR zoom decode dw=%d dh=%d want %dx%d\n", thumbDw, thumbDh,
                  cw, ch);
    return false;
  }

  strncpy(sZoomPath, path, sizeof(sZoomPath) - 1);
  sZoomPath[sizeof(sZoomPath) - 1] = '\0';
  sZoomLevel = 2;
  sZoomW = cw;
  sZoomH = ch;
  Serial.printf("zoom cache 2x %dx%d (%u KB)\n", cw, ch,
                (unsigned)(need * sizeof(uint16_t) / 1024));
  return true;
}

// Nearest-neighbor 2× upscale of a (w/2)×(h/2) window → w×h on screen.
static void blitRgb565Region2x(int16_t dx, int16_t dy, int16_t w, int16_t h,
                               const uint16_t *src, int srcStride, int sx,
                               int sy) {
  if (!src || w < 2 || h < 2) {
    return;
  }
  const int srcW = w / 2;
  const int srcH = h / 2;
  // One scanline buffer in PSRAM/stack — 320 pixels fits on stack.
  uint16_t line[320];
  if (w > 320) {
    return;
  }
  reclaimDisplay();
  tft.startWrite();
  tft.setAddrWindow(dx, dy, (uint16_t)w, (uint16_t)h);
  for (int row = 0; row < srcH; ++row) {
    const uint16_t *s =
        src + (size_t)(sy + row) * (size_t)srcStride + (size_t)sx;
    for (int col = 0; col < srcW; ++col) {
      uint16_t px = s[col];
      line[col * 2] = px;
      line[col * 2 + 1] = px;
    }
    // Write each source row twice (2× vertical).
    tft.writePixels(line, (uint32_t)w);
    tft.writePixels(line, (uint32_t)w);
  }
  tft.endWrite();
}

bool loadImageCoverToBuffer(const char *path, uint16_t *dst, int w, int h) {
  if (!dst || w < 8 || h < 8 || w > kThumbMaxW || h > kThumbMaxH) {
    return false;
  }
  bool isJpeg = false, isPng = false;
  if (!peekImageType(path, &isJpeg, &isPng)) {
    return false;
  }
  thumbOut = dst;
  bool ok = renderToThumb(path, w, h, isJpeg, true);
  thumbOut = thumbBuf;
  return ok;
}

bool drawImageInRect(const char *path, int16_t x, int16_t y, int16_t w,
                     int16_t h, bool cover) {
  if (!path || w < 8 || h < 8) {
    return false;
  }
  bool isJpeg = false, isPng = false;
  if (!peekImageType(path, &isJpeg, &isPng)) {
    return false;
  }

  // Decode into RAM first so the screen (e.g. "Loading…") stays visible.
  thumbOut = nullptr; // renderToThumb uses shared buffer
  if (!renderToThumb(path, w, h, isJpeg, cover)) {
    return false;
  }

  reclaimDisplay();
  if (!cover) {
    // letterbox bg — clip fill
    int16_t y0, y1;
    if (uiClipSpan(y, h, &y0, &y1)) {
      tft.fillRect(x, y0, w, y1 - y0, 0x2104);
    }
  }

  int16_t bx = cover ? x : (int16_t)(x + (w - thumbDw) / 2);
  int16_t by = cover ? y : (int16_t)(y + (h - thumbDh) / 2);
  blitRgb565(bx, by, (int16_t)thumbDw, (int16_t)thumbDh, thumbOut);
  return true;
}

bool drawImageFile(const char *path) {
  return drawImageZoomed(path, 1, 0.5f, 0.5f);
}

bool drawImageZoomed(const char *path, int zoom, float panX, float panY) {
  if (!path || !storageBegin()) {
    return false;
  }
  if (zoom < 1) {
    zoom = 1;
  }
  if (zoom > 4) {
    zoom = 4;
  }
  if (zoom == 3) {
    zoom = 4;
  }
  if (panX < 0.f) {
    panX = 0.f;
  }
  if (panX > 1.f) {
    panX = 1.f;
  }
  if (panY < 0.f) {
    panY = 0.f;
  }
  if (panY > 1.f) {
    panY = 1.f;
  }

  const int sw = tft.width();
  const int sh = tft.height();

  // 1×: decode into buffer first so any on-screen "Loading…" stays visible,
  // then blit once (cover fills the panel).
  if (zoom <= 1) {
    thumbZoom = 1;
    thumbPanX = 0.5f;
    thumbPanY = 0.5f;
    return drawImageInRect(path, 0, 0, (int16_t)sw, (int16_t)sh, true);
  }

  // Zoom≥2: one 2× PSRAM canvas; pan is a blit (4× = 2× digital from canvas).
  if (ensureZoomCanvas(path, sw, sh)) {
    if (zoom <= 2) {
      int maxOX = sZoomW - sw;
      int maxOY = sZoomH - sh;
      if (maxOX < 0) {
        maxOX = 0;
      }
      if (maxOY < 0) {
        maxOY = 0;
      }
      const int ox = (int)(panX * (float)maxOX + 0.5f);
      const int oy = (int)(panY * (float)maxOY + 0.5f);
      blitRgb565Region(0, 0, (int16_t)sw, (int16_t)sh, sZoomPx, sZoomW, ox, oy);
      return true;
    }
    // 4×: half-size window from 2× canvas, nearest-neighbor ×2 to panel.
    const int vw = sw / 2;
    const int vh = sh / 2;
    int maxOX = sZoomW - vw;
    int maxOY = sZoomH - vh;
    if (maxOX < 0) {
      maxOX = 0;
    }
    if (maxOY < 0) {
      maxOY = 0;
    }
    const int ox = (int)(panX * (float)maxOX + 0.5f);
    const int oy = (int)(panY * (float)maxOY + 0.5f);
    blitRgb565Region2x(0, 0, (int16_t)sw, (int16_t)sh, sZoomPx, sZoomW, ox, oy);
    return true;
  }

  // Fallback: crop-during-decode (slower, but works if PSRAM is tight).
  thumbZoom = zoom;
  thumbPanX = panX;
  thumbPanY = panY;
  bool ok = drawImageInRect(path, 0, 0, (int16_t)sw, (int16_t)sh, true);
  thumbZoom = 1;
  thumbPanX = 0.5f;
  thumbPanY = 0.5f;
  return ok;
}

bool littlefsBegin() { return storageBegin(); }
