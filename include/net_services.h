#pragma once

#include <Arduino.h>

bool connectWifi();
// Generate (or edit) image, save to gallery. Optionally draw fullscreen.
// outPath receives saved path. referencePath, if set, is sent as input_references.
bool generateAndShowImage(const String &promptIn, char *outPath = nullptr,
                          size_t outLen = 0, bool draw = true,
                          const char *referencePath = nullptr);
// OpenRouter chat (SSE stream). roles[i]/contents[i] are prior turns.
// onChunk may be null; called periodically with the full reply so far.
// On success, *outOwned is a PSRAM/heap buffer the caller must free().
using ChatChunkCb = void (*)(void *ctx, const char *textSoFar, size_t len);
bool openRouterChat(const char *systemPrompt, const char *const *roles,
                    const char *const *contents, int count, char **outOwned,
                    size_t *outLen, ChatChunkCb onChunk = nullptr,
                    void *chunkCtx = nullptr);
bool littlefsBegin();
