#pragma once

#include "window.h"

Window *windowMakePhoto();
// Make Photo with an existing gallery/preview image as edit reference.
Window *windowMakePhotoModify(const char *imagePath);
