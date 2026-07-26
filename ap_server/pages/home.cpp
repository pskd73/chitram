#include "registry.h"

#include "settings.h"

// SoftAP home page.
static const char kIndexHtml[] = R"HTML(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="stylesheet" href="/style.css">
  <title>{{device_name}}</title>
</head>
<body>
  {{import.navbar}}
  <main class="page">
    <h1>{{device_name}}</h1>
    <p class="muted">Device dashboard</p>
  </main>
</body>
</html>
)HTML";

static void handleHome(WebServer &server) {
  settingsBegin();
  TmplVar vars[] = {
      {"device_name", settingsDeviceName()},
      {"gallery_active", ""},
      {"settings_active", ""},
  };
  apSendTemplate(server, kIndexHtml, vars, 3);
}

static const ApGetReg kHome[] = {
    {"/", handleHome},
};
static ApAutoRegister regHome(kHome, 1);
