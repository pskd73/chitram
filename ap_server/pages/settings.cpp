#include "registry.h"

#include "settings.h"

#include <Arduino.h>

// SoftAP settings — editable network fields + read-only generation info.
static const char kSettingsHtml[] = R"HTML(
<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="stylesheet" href="/style.css">
  <title>Settings — {{device_name}}</title>
</head>
<body>
  {{import.navbar}}
  <main class="page">
    <h1>Settings</h1>
    <p class="muted">{{status}}</p>
    <form class="settings-form" method="POST" action="/settings">
      <label class="form-control w-full">
        <span class="label"><span class="label-text">Device name</span></span>
        <input class="input input-bordered w-full" name="device_name" required maxlength="31" value="{{device_name_value}}">
        <span class="label"><span class="label-text-alt">Also used as SoftAP Wi‑Fi name</span></span>
      </label>
      <label class="form-control w-full">
        <span class="label"><span class="label-text">Wi‑Fi username (SSID)</span></span>
        <input class="input input-bordered w-full" name="wifi_ssid" required maxlength="31" value="{{wifi_ssid}}">
      </label>
      <label class="form-control w-full">
        <span class="label"><span class="label-text">Wi‑Fi password</span></span>
        <input class="input input-bordered w-full" type="password" name="wifi_password" maxlength="63" value="{{wifi_password}}" autocomplete="off">
      </label>
      <label class="form-control w-full">
        <span class="label"><span class="label-text">AP server password</span></span>
        <input class="input input-bordered w-full" type="password" name="ap_password" required minlength="8" maxlength="63" value="{{ap_password}}" autocomplete="off">
        <span class="label"><span class="label-text-alt">SoftAP password · at least 8 characters · applies next time Web Wi‑Fi starts</span></span>
      </label>
      <label class="label cursor-pointer justify-start gap-3 py-1">
        <input type="checkbox" class="checkbox checkbox-primary" name="save_audio" value="1" {{save_audio_checked}}>
        <span class="label-text">Save Ask audio to SD card</span>
      </label>
      <span class="label-text-alt text-base-content/60 -mt-2">Writes /audio/*.wav when Ask listens · uses extra PSRAM · default off</span>
      <button class="btn btn-primary mt-2" type="submit">Save</button>
    </form>
    <h2 class="settings-sub">Generation</h2>
    <dl class="settings-list">
      <div>
        <dt>Image model</dt>
        <dd>{{model}}</dd>
      </div>
      <div>
        <dt>Aspect ratio</dt>
        <dd>{{aspect}}</dd>
      </div>
      <div>
        <dt>Resolution</dt>
        <dd>{{resolution}}</dd>
      </div>
    </dl>
  </main>
</body>
</html>
)HTML";

static void htmlEscape(const char *in, String &out) {
  out = "";
  if (!in) {
    return;
  }
  for (const char *p = in; *p; ++p) {
    switch (*p) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += *p;
      break;
    }
  }
}

static void renderSettings(WebServer &server, const char *status) {
  settingsBegin();
  const char *model = settingsImageModelLabel(settingsImageModel());
  const char *aspect = settingsAspectRatioLabel(settingsAspectRatio());
  const char *resolution = settingsResolutionLabel(settingsResolution());

  String deviceEsc, wifiEsc, wifiPassEsc, apPassEsc;
  htmlEscape(settingsDeviceName(), deviceEsc);
  htmlEscape(settingsWifiSsid(), wifiEsc);
  htmlEscape(settingsWifiPassword(), wifiPassEsc);
  htmlEscape(settingsApPassword(), apPassEsc);

  TmplVar vars[] = {
      {"device_name", settingsDeviceName()},
      {"device_name_value", deviceEsc.c_str()},
      {"gallery_active", ""},
      {"settings_active", "class=\"menu-active\""},
      {"status", status ? status : "Device and network configuration"},
      {"wifi_ssid", wifiEsc.c_str()},
      {"wifi_password", wifiPassEsc.c_str()},
      {"ap_password", apPassEsc.c_str()},
      {"save_audio_checked", settingsSaveAudio() ? "checked" : ""},
      {"model", model ? model : settingsImageModel()},
      {"aspect", aspect ? aspect : settingsAspectRatio()},
      {"resolution", resolution ? resolution : settingsResolution()},
  };
  apSendTemplate(server, kSettingsHtml, vars, 12);
}

static void handleSettingsGet(WebServer &server) {
  const char *status = "Device and network configuration";
  if (server.hasArg("saved")) {
    status = "Saved. SoftAP name/password apply next time Web Wi‑Fi starts.";
  } else if (server.hasArg("err")) {
    status = "Could not save — check fields (AP password needs 8+ characters).";
  }
  renderSettings(server, status);
}

static void handleSettingsPost(WebServer &server) {
  settingsBegin();
  bool ok = true;
  if (server.hasArg("device_name")) {
    ok = settingsSetDeviceName(server.arg("device_name").c_str()) && ok;
  }
  if (server.hasArg("wifi_ssid")) {
    ok = settingsSetWifiSsid(server.arg("wifi_ssid").c_str()) && ok;
  }
  if (server.hasArg("wifi_password")) {
    ok = settingsSetWifiPassword(server.arg("wifi_password").c_str()) && ok;
  }
  if (server.hasArg("ap_password")) {
    ok = settingsSetApPassword(server.arg("ap_password").c_str()) && ok;
  }
  // Unchecked checkboxes are omitted from POST bodies.
  ok = settingsSetSaveAudio(server.hasArg("save_audio")) && ok;
  apRedirect(server, ok ? "/settings?saved=1" : "/settings?err=1");
}

static const ApGetReg kSettingsGet[] = {
    {"/settings", handleSettingsGet},
};
static ApAutoRegister regSettingsGet(kSettingsGet, 1);

static const ApPostReg kSettingsPost[] = {
    {"/settings", handleSettingsPost},
};
static ApAutoRegisterPost regSettingsPost(kSettingsPost, 1);
