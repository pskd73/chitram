#pragma once

#include "profiles.h"
#include "window.h"

// type-filtered list. On select: set active profile and replaceTop feature.
Window *windowProfilePicker(ProfileType type);
