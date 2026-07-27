#pragma once

#include "window.h"
#include <Arduino.h>

// Called when user confirms the transcript.
using SttConfirmCb = void (*)(void *ctx, const char *text);
// Called when user cancels.
using SttCancelCb  = void (*)(void *ctx);

// Returns a full-screen Window that:
//   1. Immediately starts an STT session (Connecting → Listening).
//   2. On finish shows transcript with Confirm / Re-record / Cancel menu.
//   3. On Confirm: calls onConfirm(ctx, text) then pops itself.
//   4. On Cancel:  calls onCancel(ctx) then pops itself.
// The window pops itself — caller should just push it.
Window *windowSttInput(const char *title, const char *icon,
                       SttConfirmCb onConfirm, SttCancelCb onCancel,
                       void *ctx);
