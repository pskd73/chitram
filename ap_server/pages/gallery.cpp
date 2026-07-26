#include "registry.h"

#include "gallery.h"
#include "settings.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>

// SoftAP gallery page ({{count}} / {{gallery}} filled at request time).
static const char kGalleryHtml[] = R"HTML(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="stylesheet" href="/style.css">
  <title>Gallery — {{device_name}}</title>
</head>
<body>
  {{import.navbar}}
  <main class="page">
    <h1>Gallery</h1>
    <p class="muted">{{count}} photos</p>
    <div class="grid">{{gallery}}</div>
  </main>
</body>
</html>
)HTML";

static const char *mimeForPath(const char *path) {
  if (!path) {
    return "application/octet-stream";
  }
  const char *dot = strrchr(path, '.');
  if (!dot) {
    return "application/octet-stream";
  }
  if (strcasecmp(dot, ".png") == 0) {
    return "image/png";
  }
  if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
    return "image/jpeg";
  }
  return "application/octet-stream";
}

static void handleGallery(WebServer &server) {
  if (!storageBegin()) {
    server.send(500, "text/plain", "storage unavailable");
    return;
  }

  int n = galleryCount();
  char countBuf[12];
  snprintf(countBuf, sizeof(countBuf), "%d", n);

  String grid;
  if (n <= 0) {
    grid = "<p class=\"empty\">No photos yet.</p>";
  } else {
    grid.reserve((size_t)n * 96);
    for (int display = 0; display < n; ++display) {
      int idx = n - 1 - display;
      char item[120];
      snprintf(item, sizeof(item),
               "<a href=\"/gallery/photo?i=%d\">"
               "<img src=\"/gallery/thumb?i=%d\" loading=\"lazy\" alt=\"\">"
               "</a>",
               idx, idx);
      grid += item;
    }
  }

  TmplVar vars[] = {
      {"device_name", settingsDeviceName()},
      {"gallery_active", "class=\"menu-active\""},
      {"settings_active", ""},
      {"count", countBuf},
      {"gallery", grid.c_str()},
  };
  apSendTemplate(server, kGalleryHtml, vars, 5);
}

static void handlePhoto(WebServer &server) {
  if (!server.hasArg("i")) {
    server.send(400, "text/plain", "missing i");
    return;
  }
  int idx = server.arg("i").toInt();
  if (!storageBegin()) {
    server.send(500, "text/plain", "storage unavailable");
    return;
  }
  char path[40];
  if (!galleryPathAt(idx, path, sizeof(path))) {
    server.send(404, "text/plain", "not found");
    return;
  }
  File f = imageFs().open(path, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "not found");
    return;
  }
  server.streamFile(f, mimeForPath(path));
  f.close();
}

static void handleThumb(WebServer &server) {
  if (!server.hasArg("i")) {
    server.send(400, "text/plain", "missing i");
    return;
  }
  int idx = server.arg("i").toInt();
  if (!storageBegin()) {
    server.send(500, "text/plain", "storage unavailable");
    return;
  }
  char path[48];
  if (galleryThumbPathAt(idx, path, sizeof(path))) {
    File f = imageFs().open(path, FILE_READ);
    if (f) {
      server.streamFile(f, "image/jpeg");
      f.close();
      return;
    }
  }
  // Fallback: full image (older gallery entries without thumbs)
  if (!galleryPathAt(idx, path, sizeof(path))) {
    server.send(404, "text/plain", "not found");
    return;
  }
  File f = imageFs().open(path, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "not found");
    return;
  }
  server.streamFile(f, mimeForPath(path));
  f.close();
}

static const ApGetReg kGallery[] = {
    {"/gallery", handleGallery},
    {"/gallery/photo", handlePhoto},
    {"/gallery/thumb", handleThumb},
};
static ApAutoRegister regGallery(kGallery, 3);
