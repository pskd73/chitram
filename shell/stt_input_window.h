#pragma once

#include "window.h"
#include <Arduino.h>

// Called when user confirms the transcript.
using SttConfirmCb = void (*)(void *ctx, const char *text);
// Called when user cancels.
using SttCancelCb = void (*)(void *ctx);

// Optional overrides for windowSttInput. Defaults match the standard review UI.
struct SttInputOpts {
  // If true, skip Confirm/Re-record/Cancel and call onConfirm as soon as STT
  // finishes (Ok to stop while listening).
  bool autoConfirm = false;
  // Menu labels (nullptr = default).
  const char *confirmLabel = nullptr;  // default "Confirm"
  const char *rerecordLabel = nullptr; // default "Re-record"
  const char *cancelLabel = nullptr;   // default "Cancel"
};

// Full-screen listen window:
//   1. Starts STT (Connecting → Listening).
//   2. By default, after stop shows Confirm / Re-record / Cancel.
//   3. Confirm → onConfirm(ctx, text) then pop.
//   4. Cancel  → onCancel(ctx) then pop.
// Pass SttInputOpts to override labels or enable autoConfirm.
Window *windowSttInput(const char *title, const char *icon,
                       SttConfirmCb onConfirm, SttCancelCb onCancel, void *ctx,
                       const SttInputOpts &opts = SttInputOpts{});
