#pragma once

#include "window.h"

// Fullscreen photo viewer with zoom/pan.
// Single path (e.g. after generate) — no gallery browse.
Window *windowPhoto(const char *path);
// Gallery browse starting at abs index (0 = oldest).
Window *windowPhotoGallery(int index);
// Alias used by Ask generate flow.
Window *windowImagePreview(const char *path);
// Menu over a photo: Zoom in/out, Edit, Delete.
Window *windowPhotoMenu(const char *imagePath);
