#pragma once

#include <Arduino.h>

#define GALLERY_DIR "/gallery"
#define GALLERY_THUMB_DIR "/gallery/thumbnails"
#define GALLERY_THUMB_W 160
#define GALLERY_THUMB_H 112 // multiple of 16 for JPEG MCU
#define GALLERY_MAX_FILES 200
#define GALLERY_NAME_LEN 32
#define GALLERY_GRID_MAX 10
#define GALLERY_GRID_COLS 5
#define GALLERY_GRID_ROWS 2

bool galleryEnsureDir();
// Move temp download (IMAGE_FS_PATH) into /gallery/NNNNN.jpg|.png. Returns path in out.
// Also builds /gallery/thumbnails/NNNNN.jpg when possible.
bool gallerySaveFromTemp(const char *tempPath, bool isJpeg, char *outPath,
                         size_t outLen);
// Encode RGB565 framebuffer to /gallery/NNNNN.jpg (+ thumb). outPath optional.
bool gallerySaveRgb565(const uint16_t *rgb, int width, int height,
                       char *outPath = nullptr, size_t outLen = 0);
// Capture current display mirror and save to gallery.
bool gallerySaveScreenshot(char *outPath = nullptr, size_t outLen = 0);
// Remove a gallery file by full path (must be under GALLERY_DIR); removes thumb too.
bool galleryDelete(const char *path);
// Full path → thumb path (/gallery/00001.jpg → /gallery/thumbnails/00001.jpg).
bool galleryThumbPathFor(const char *imagePath, char *outPath, size_t outLen);
// Thumbnail for gallery index; false if missing.
bool galleryThumbPathAt(int index, char *outPath, size_t outLen);
// Decode + JPEG-encode a small thumb next to an existing gallery image.
bool galleryMakeThumb(const char *imagePath);
// Rebuild all on-disk thumbs from full gallery images. Returns count rebuilt.
int galleryRebuildThumbs();
int galleryCount();
// index 0 = oldest. Returns false if empty/out of range.
bool galleryPathAt(int index, char *outPath, size_t outLen);
bool galleryDrawAt(int index);
int galleryLatestIndex(); // count-1, or -1 if empty
// 2x5 grid of the newest up to GALLERY_GRID_MAX images.
bool galleryDrawGrid();
// Delete all gallery images and thumbnails. Returns count removed, or -1 on error.
int galleryClearAll();
