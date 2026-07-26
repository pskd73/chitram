#pragma once

#include "window.h"

Window *windowAsk();
// Ask with an existing gallery/preview image as edit reference.
Window *windowAskModify(const char *imagePath);
