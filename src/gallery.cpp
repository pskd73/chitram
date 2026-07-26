#include "gallery.h"
#include "config.h"
#include "storage.h"
#include "image_draw.h"
#include "display.h"

#include <Adafruit_ILI9341.h>
#include <JPEGENC.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool isGalleryImageName(const char *name) {
  if (!name || name[0] == '.') {
    return false;
  }
  const char *dot = strrchr(name, '.');
  if (!dot || dot == name) {
    return false;
  }
  for (const char *p = name; p < dot; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
  }
  return strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 ||
         strcasecmp(dot, ".png") == 0;
}

static int parseGalleryIndex(const char *name) {
  if (!name) {
    return -1;
  }
  int v = 0;
  const char *p = name;
  if (*p < '0' || *p > '9') {
    return -1;
  }
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    ++p;
    if (v > 999999) {
      return -1;
    }
  }
  if (*p != '.') {
    return -1;
  }
  return v;
}

bool galleryEnsureDir() {
  if (!storageBegin()) {
    return false;
  }
  if (!imageFs().exists(GALLERY_DIR)) {
    if (!imageFs().mkdir(GALLERY_DIR)) {
      Serial.println("ERR mkdir /gallery");
      return false;
    }
  }
  if (!imageFs().exists(GALLERY_THUMB_DIR)) {
    if (!imageFs().mkdir(GALLERY_THUMB_DIR)) {
      Serial.println("ERR mkdir /gallery/thumbnails");
      return false;
    }
  }
  return true;
}

static int galleryMaxSeq() {
  int maxSeq = 0;
  File dir = imageFs().open(GALLERY_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }
  File f = dir.openNextFile();
  while (f) {
    const char *name = f.name();
    // SD may return "/gallery/00001.jpg" or "00001.jpg"
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    int seq = parseGalleryIndex(base);
    if (seq > maxSeq) {
      maxSeq = seq;
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  return maxSeq;
}

bool gallerySaveFromTemp(const char *tempPath, bool isJpeg, char *outPath,
                         size_t outLen) {
  if (!galleryEnsureDir() || !tempPath || !outPath || outLen < 20) {
    return false;
  }
  int seq = galleryMaxSeq() + 1;
  if (seq > 99999) {
    seq = 1;
  }
  // Cap file count: if too many, still save (user can clear card)
  snprintf(outPath, outLen, "%s/%05d.%s", GALLERY_DIR, seq,
           isJpeg ? "jpg" : "png");
  imageFs().remove(outPath);
  if (!imageFs().rename(tempPath, outPath)) {
    // rename can fail across some FS impls — copy then remove
    File in = imageFs().open(tempPath, FILE_READ);
    File out = imageFs().open(outPath, FILE_WRITE);
    if (!in || !out) {
      if (in) {
        in.close();
      }
      if (out) {
        out.close();
      }
      Serial.println("ERR gallery save open");
      return false;
    }
    uint8_t buf[1024];
    while (true) {
      int n = in.read(buf, sizeof(buf));
      if (n <= 0) {
        break;
      }
      if ((int)out.write(buf, n) != n) {
        in.close();
        out.close();
        imageFs().remove(outPath);
        Serial.println("ERR gallery copy write");
        return false;
      }
    }
    in.close();
    out.close();
    imageFs().remove(tempPath);
  }
  Serial.printf("gallery saved %s\n", outPath);
  if (!galleryMakeThumb(outPath)) {
    Serial.println("WARN gallery thumb failed");
  }
  return true;
}

bool gallerySaveRgb565(const uint16_t *rgb, int width, int height,
                       char *outPath, size_t outLen) {
  if (!rgb || width < 16 || height < 16 || !galleryEnsureDir()) {
    return false;
  }
  // JPEGENC MCUs are 16×16
  if ((width % 16) != 0 || (height % 16) != 0) {
    Serial.printf("ERR screenshot size %dx%d not MCU-aligned\n", width, height);
    return false;
  }

  char path[48];
  char *dest = outPath && outLen >= 20 ? outPath : path;
  size_t destLen = outPath && outLen >= 20 ? outLen : sizeof(path);
  int seq = galleryMaxSeq() + 1;
  if (seq > 99999) {
    seq = 1;
  }
  snprintf(dest, destLen, "%s/%05d.jpg", GALLERY_DIR, seq);

  static JPEGENC sJpg;
  // ~80KB is enough for 320×240 Q_MED; keep off stack
  static uint8_t *sJpegBuf = nullptr;
  static size_t sJpegCap = 0;
  const size_t need = 96 * 1024;
  if (!sJpegBuf || sJpegCap < need) {
    if (sJpegBuf) {
      free(sJpegBuf);
    }
    sJpegBuf = (uint8_t *)ps_malloc(need);
    if (!sJpegBuf) {
      sJpegBuf = (uint8_t *)malloc(need);
    }
    sJpegCap = sJpegBuf ? need : 0;
  }
  if (!sJpegBuf) {
    Serial.println("ERR screenshot jpeg buf");
    return false;
  }

  JPEGENCODE enc;
  if (sJpg.open(sJpegBuf, (int)sJpegCap) != JPEGE_SUCCESS) {
    Serial.println("ERR screenshot jpeg open");
    return false;
  }
  if (sJpg.encodeBegin(&enc, width, height, JPEGE_PIXEL_RGB565,
                       JPEGE_SUBSAMPLE_420, JPEGE_Q_MED) != JPEGE_SUCCESS) {
    sJpg.close();
    Serial.println("ERR screenshot jpeg begin");
    return false;
  }

  const int pitch = width * (int)sizeof(uint16_t);
  yield();
  if (sJpg.addFrame(&enc, (uint8_t *)rgb, pitch) != JPEGE_SUCCESS) {
    sJpg.close();
    Serial.println("ERR screenshot jpeg encode");
    return false;
  }
  int jpegLen = sJpg.close();
  if (jpegLen <= 0) {
    Serial.println("ERR screenshot jpeg encode");
    return false;
  }

  imageFs().remove(dest);
  File out = imageFs().open(dest, FILE_WRITE);
  if (!out) {
    Serial.println("ERR screenshot open out");
    return false;
  }
  if ((int)out.write(sJpegBuf, jpegLen) != jpegLen) {
    out.close();
    imageFs().remove(dest);
    Serial.println("ERR screenshot write");
    return false;
  }
  out.close();
  Serial.printf("gallery screenshot %s (%d bytes)\n", dest, jpegLen);
  if (!galleryMakeThumb(dest)) {
    Serial.println("WARN screenshot thumb failed");
  }
  if (outPath && outLen && dest != outPath) {
    strncpy(outPath, dest, outLen - 1);
    outPath[outLen - 1] = '\0';
  }
  return true;
}

bool gallerySaveScreenshot(char *outPath, size_t outLen) {
  const uint16_t *fb = tft.framebuffer();
  if (!fb) {
    Serial.println("ERR screenshot: no framebuffer");
    return false;
  }
  return gallerySaveRgb565(fb, tft.fbWidth(), tft.fbHeight(), outPath, outLen);
}

bool galleryThumbPathFor(const char *imagePath, char *outPath, size_t outLen) {
  if (!imagePath || !outPath || outLen < 28) {
    return false;
  }
  const char *base = strrchr(imagePath, '/');
  base = base ? base + 1 : imagePath;
  // Keep numeric stem; always .jpg for thumbs
  char stem[16];
  size_t i = 0;
  while (base[i] && base[i] != '.' && i + 1 < sizeof(stem)) {
    stem[i] = base[i];
    ++i;
  }
  stem[i] = '\0';
  if (i == 0) {
    return false;
  }
  snprintf(outPath, outLen, "%s/%s.jpg", GALLERY_THUMB_DIR, stem);
  return true;
}

bool galleryThumbPathAt(int index, char *outPath, size_t outLen) {
  char full[48];
  if (!galleryPathAt(index, full, sizeof(full))) {
    return false;
  }
  if (!galleryThumbPathFor(full, outPath, outLen)) {
    return false;
  }
  return imageFs().exists(outPath);
}

bool galleryMakeThumb(const char *imagePath) {
  if (!imagePath || !imagePath[0] || !galleryEnsureDir()) {
    return false;
  }
  char thumbPath[48];
  if (!galleryThumbPathFor(imagePath, thumbPath, sizeof(thumbPath))) {
    return false;
  }

  // Keep large objects off the loop-task stack (generate path is already deep).
  static JPEGENC sJpg;
  static uint16_t sRgb[GALLERY_THUMB_W * GALLERY_THUMB_H];
  static uint8_t sJpegBuf[24 * 1024];

  const int tw = GALLERY_THUMB_W;
  const int th = GALLERY_THUMB_H;
  memset(sRgb, 0, sizeof(sRgb));

  Serial.printf("thumb: decode %s heap=%u\n", imagePath,
                (unsigned)ESP.getFreeHeap());
  yield();
  if (!loadImageCoverToBuffer(imagePath, sRgb, tw, th)) {
    Serial.printf("ERR thumb decode %s\n", imagePath);
    return false;
  }
  yield();

  JPEGENCODE enc;
  if (sJpg.open(sJpegBuf, (int)sizeof(sJpegBuf)) != JPEGE_SUCCESS) {
    Serial.println("ERR thumb jpeg open");
    return false;
  }
  if (sJpg.encodeBegin(&enc, tw, th, JPEGE_PIXEL_RGB565, JPEGE_SUBSAMPLE_420,
                       JPEGE_Q_LOW) != JPEGE_SUCCESS) {
    sJpg.close();
    Serial.println("ERR thumb jpeg begin");
    return false;
  }

  const int pitch = tw * (int)sizeof(uint16_t);
  yield();
  if (sJpg.addFrame(&enc, (uint8_t *)sRgb, pitch) != JPEGE_SUCCESS) {
    sJpg.close();
    Serial.println("ERR thumb jpeg encode");
    return false;
  }
  int jpegLen = sJpg.close();
  if (jpegLen <= 0) {
    Serial.println("ERR thumb jpeg encode");
    return false;
  }

  imageFs().remove(thumbPath);
  File out = imageFs().open(thumbPath, FILE_WRITE);
  if (!out) {
    Serial.printf("ERR thumb write open %s\n", thumbPath);
    return false;
  }
  size_t wrote = out.write(sJpegBuf, (size_t)jpegLen);
  out.close();
  if (wrote != (size_t)jpegLen) {
    imageFs().remove(thumbPath);
    Serial.println("ERR thumb write short");
    return false;
  }
  Serial.printf("gallery thumb %s (%d bytes)\n", thumbPath, jpegLen);
  return true;
}

int galleryRebuildThumbs() {
  if (!galleryEnsureDir()) {
    return -1;
  }
  int n = galleryCount();
  int ok = 0;
  for (int i = 0; i < n; ++i) {
    char path[48];
    if (!galleryPathAt(i, path, sizeof(path))) {
      continue;
    }
    yield();
    if (galleryMakeThumb(path)) {
      ++ok;
    }
  }
  Serial.printf("gallery rebuild thumbs %d/%d\n", ok, n);
  return ok;
}

bool galleryDelete(const char *path) {
  if (!path || !path[0]) {
    return false;
  }
  // Only allow deleting files under /gallery
  const size_t dirLen = strlen(GALLERY_DIR);
  if (strncmp(path, GALLERY_DIR, dirLen) != 0 || path[dirLen] != '/') {
    Serial.printf("ERR gallery delete bad path %s\n", path);
    return false;
  }
  if (!storageBegin()) {
    return false;
  }
  if (!imageFs().exists(path)) {
    Serial.printf("ERR gallery delete missing %s\n", path);
    return false;
  }
  char thumbPath[48];
  if (galleryThumbPathFor(path, thumbPath, sizeof(thumbPath))) {
    imageFs().remove(thumbPath);
  }
  if (!imageFs().remove(path)) {
    Serial.printf("ERR gallery delete fail %s\n", path);
    return false;
  }
  Serial.printf("gallery deleted %s\n", path);
  return true;
}

static int clearDirImages(const char *dirPath, bool thumbsOnly) {
  // Delete one file per pass — avoids a huge stack buffer and SD
  // iterator invalidation while removing.
  int removed = 0;
  for (;;) {
    File dir = imageFs().open(dirPath);
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        dir.close();
      }
      break;
    }

    char victim[GALLERY_NAME_LEN] = {};
    File f = dir.openNextFile();
    while (f) {
      const char *name = f.name();
      const char *base = strrchr(name, '/');
      base = base ? base + 1 : name;
      bool take = false;
      if (!f.isDirectory()) {
        if (thumbsOnly) {
          take = base[0] && base[0] != '.';
        } else {
          take = isGalleryImageName(base);
        }
      }
      if (take) {
        strncpy(victim, base, GALLERY_NAME_LEN - 1);
        victim[GALLERY_NAME_LEN - 1] = '\0';
        f.close();
        break;
      }
      f.close();
      f = dir.openNextFile();
    }
    dir.close();

    if (!victim[0]) {
      break;
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", dirPath, victim);
    if (!imageFs().remove(path)) {
      Serial.printf("ERR gallery clear remove %s\n", path);
      break; // avoid infinite loop if remove always fails
    }
    ++removed;
    yield();
  }
  return removed;
}

int galleryClearAll() {
  if (!storageBegin()) {
    return -1;
  }
  // Don't require mkdir — clearing should work even if dirs are half-missing
  int thumbs = 0;
  int photos = 0;
  if (imageFs().exists(GALLERY_THUMB_DIR)) {
    thumbs = clearDirImages(GALLERY_THUMB_DIR, true);
  }
  if (imageFs().exists(GALLERY_DIR)) {
    photos = clearDirImages(GALLERY_DIR, false);
  }
  Serial.printf("gallery clear photos=%d thumbs=%d\n", photos, thumbs);
  return photos;
}

int galleryCount() {
  if (!galleryEnsureDir()) {
    return 0;
  }
  int n = 0;
  File dir = imageFs().open(GALLERY_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }
  File f = dir.openNextFile();
  while (f) {
    const char *name = f.name();
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (!f.isDirectory() && isGalleryImageName(base)) {
      ++n;
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  return n;
}

static int fillSortedIndices(int *idxs, int maxN) {
  int n = 0;
  File dir = imageFs().open(GALLERY_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }
  File f = dir.openNextFile();
  while (f) {
    const char *name = f.name();
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    int seq = parseGalleryIndex(base);
    if (!f.isDirectory() && seq >= 0 && isGalleryImageName(base)) {
      if (n < maxN) {
        idxs[n++] = seq;
      }
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  // insertion sort ascending
  for (int i = 1; i < n; ++i) {
    int key = idxs[i];
    int j = i - 1;
    while (j >= 0 && idxs[j] > key) {
      idxs[j + 1] = idxs[j];
      --j;
    }
    idxs[j + 1] = key;
  }
  return n;
}

bool galleryPathAt(int index, char *outPath, size_t outLen) {
  if (!outPath || outLen < 24 || index < 0) {
    return false;
  }
  int idxs[GALLERY_MAX_FILES];
  int n = fillSortedIndices(idxs, GALLERY_MAX_FILES);
  if (index >= n) {
    return false;
  }
  int seq = idxs[index];
  // Prefer .jpg if both somehow exist
  char jpg[GALLERY_NAME_LEN];
  char png[GALLERY_NAME_LEN];
  snprintf(jpg, sizeof(jpg), "%s/%05d.jpg", GALLERY_DIR, seq);
  snprintf(png, sizeof(png), "%s/%05d.png", GALLERY_DIR, seq);
  if (imageFs().exists(jpg)) {
    strncpy(outPath, jpg, outLen - 1);
    outPath[outLen - 1] = '\0';
    return true;
  }
  if (imageFs().exists(png)) {
    strncpy(outPath, png, outLen - 1);
    outPath[outLen - 1] = '\0';
    return true;
  }
  return false;
}

int galleryLatestIndex() {
  int n = galleryCount();
  return n > 0 ? n - 1 : -1;
}

bool galleryDrawAt(int index) {
  char path[40];
  if (!galleryPathAt(index, path, sizeof(path))) {
    return false;
  }
  return drawImageFile(path);
}

bool galleryDrawGrid() {
  if (!galleryEnsureDir()) {
    return false;
  }

  reclaimDisplay();
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(8, 4);
  tft.print("Gallery");
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(200, 4);
  tft.print("hold = exit");

  int total = galleryCount();
  if (total <= 0) {
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(60, 110);
    tft.print("No images");
    return true;
  }

  int n = total < GALLERY_GRID_MAX ? total : GALLERY_GRID_MAX;
  int start = total - n; // oldest among the last N

  const int marginX = 4;
  const int topY = 18;
  const int gap = 3;
  const int cols = GALLERY_GRID_COLS;
  const int rows = GALLERY_GRID_ROWS;
  int cellW = (tft.width() - 2 * marginX - (cols - 1) * gap) / cols;
  // Prefer ~4:3 cells
  int cellH = (cellW * 3) / 4;
  int gridH = rows * cellH + (rows - 1) * gap;
  int availH = tft.height() - topY - 4;
  if (gridH > availH) {
    cellH = (availH - (rows - 1) * gap) / rows;
    gridH = rows * cellH + (rows - 1) * gap;
  }
  int originY = topY + (availH - gridH) / 2;
  if (originY < topY) {
    originY = topY;
  }

  char path[40];
  for (int i = 0; i < n; ++i) {
    // Show newest first: left-to-right, top-to-bottom
    int newestFirst = n - 1 - i;
    int idx = start + newestFirst;
    int col = i % cols;
    int row = i / cols;
    if (row >= rows) {
      break;
    }
    int16_t x = (int16_t)(marginX + col * (cellW + gap));
    int16_t y = (int16_t)(originY + row * (cellH + gap));
    tft.drawRect(x, y, cellW, cellH, ILI9341_DARKGREY);
    if (!galleryPathAt(idx, path, sizeof(path))) {
      continue;
    }
    if (!drawImageInRect(path, (int16_t)(x + 1), (int16_t)(y + 1),
                         (int16_t)(cellW - 2), (int16_t)(cellH - 2))) {
      tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, 0x4208);
    }
  }

  // Empty slots (if fewer than 10)
  for (int i = n; i < GALLERY_GRID_MAX; ++i) {
    int col = i % cols;
    int row = i / cols;
    if (row >= rows) {
      break;
    }
    int16_t x = (int16_t)(marginX + col * (cellW + gap));
    int16_t y = (int16_t)(originY + row * (cellH + gap));
    tft.drawRect(x, y, cellW, cellH, 0x2104);
  }

  Serial.printf("gallery grid n=%d total=%d cell=%dx%d\n", n, total, cellW,
                cellH);
  return true;
}
