#pragma once

#include <stddef.h>

#define SETTINGS_PATH "/settings.json"
#define SETTINGS_MODEL_MAX 96
#define SETTINGS_VALUE_MAX 16
#define SETTINGS_NAME_MAX 32
#define SETTINGS_WIFI_PASS_MAX 64
#define SETTINGS_AP_PASS_MAX 64

struct SettingsImageModel {
  const char *label;
  const char *id;
};

struct SettingsOption {
  const char *label;
  const char *id;
};

bool settingsBegin();

const char *settingsImageModel();
bool settingsSetImageModel(const char *id);
int settingsImageModelCount();
const SettingsImageModel *settingsImageModelAt(int index);
const char *settingsImageModelLabel(const char *id);
int settingsImageModelIndex(const char *id);

const char *settingsAspectRatio();
bool settingsSetAspectRatio(const char *id);
int settingsAspectRatioCount();
const SettingsOption *settingsAspectRatioAt(int index);
const char *settingsAspectRatioLabel(const char *id);
int settingsAspectRatioIndex(const char *id);

const char *settingsResolution();
bool settingsSetResolution(const char *id);
int settingsResolutionCount();
const SettingsOption *settingsResolutionAt(int index);
const char *settingsResolutionLabel(const char *id);
int settingsResolutionIndex(const char *id);

// Device / network (persisted). SoftAP SSID uses device name.
const char *settingsDeviceName();
bool settingsSetDeviceName(const char *name);

const char *settingsWifiSsid();
bool settingsSetWifiSsid(const char *ssid);

const char *settingsWifiPassword();
bool settingsSetWifiPassword(const char *password);

const char *settingsApPassword();
bool settingsSetApPassword(const char *password);

// Save Ask PCM to /audio/*.wav on SD (uses PSRAM; default off).
bool settingsSaveAudio();
bool settingsSetSaveAudio(bool on);

// AI prompt prefix prepended to every image generation request.
#define SETTINGS_PROMPT_MAX 128
const char *settingsAiPrompt();
bool settingsSetAiPrompt(const char *text);

// SoftAP credentials derived from device name + AP password
const char *settingsShareApSsid();
const char *settingsShareApPassword();
