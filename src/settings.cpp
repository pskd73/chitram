#include "settings.h"

#include "config.h"
#include "secrets.h"
#include "storage.h"

#include <Arduino.h>
#include <string.h>

static const SettingsImageModel kImageModels[] = {
    {"GPT Image 2", "openai/gpt-image-2"},
    {"Gemini Flash Lite", "google/gemini-3.1-flash-lite-image"},
    {"Gemini 3 Pro Image", "google/gemini-3-pro-image"},
    {"GPT 5.4 Image 2", "openai/gpt-5.4-image-2"},
    {"GPT 5 Image Mini", "openai/gpt-5-image-mini"},
};
static const int kImageModelCount =
    (int)(sizeof(kImageModels) / sizeof(kImageModels[0]));

// OpenRouter normalized aspect ratios (common subset)
static const SettingsOption kAspectRatios[] = {
    {"1:1 Square", "1:1"},
    {"4:3 Landscape", "4:3"},
    {"3:4 Portrait", "3:4"},
    {"16:9 Wide", "16:9"},
    {"9:16 Tall", "9:16"},
    {"3:2", "3:2"},
    {"2:3", "2:3"},
};
static const int kAspectRatioCount =
    (int)(sizeof(kAspectRatios) / sizeof(kAspectRatios[0]));

static const SettingsOption kResolutions[] = {
    {"512", "512"},
    {"1K", "1K"},
    {"2K", "2K"},
    {"4K", "4K"},
};
static const int kResolutionCount =
    (int)(sizeof(kResolutions) / sizeof(kResolutions[0]));

static const char *kDefaultAspect = "4:3";
static const char *kDefaultResolution = "1K";
static const char *kDefaultDeviceName = "Chitram";
static const char *kDefaultApPassword = "chitram12";

static char sImageModel[SETTINGS_MODEL_MAX] = IMAGE_MODEL;
static char sAspectRatio[SETTINGS_VALUE_MAX] = "4:3";
static char sResolution[SETTINGS_VALUE_MAX] = "1K";
static char sDeviceName[SETTINGS_NAME_MAX] = "Chitram";
static char sWifiSsid[SETTINGS_NAME_MAX] = WIFI_SSID;
static char sWifiPassword[SETTINGS_WIFI_PASS_MAX] = WIFI_PASSWORD;
static char sApPassword[SETTINGS_AP_PASS_MAX] = "chitram12";
static bool sSaveAudio = false;
static char sAiPrompt[SETTINGS_PROMPT_MAX] = {};
static bool sReady = false;

static int optionIndex(const SettingsOption *opts, int count, const char *id) {
  if (!id) {
    return -1;
  }
  for (int i = 0; i < count; ++i) {
    if (strcmp(opts[i].id, id) == 0) {
      return i;
    }
  }
  return -1;
}

static const char *optionLabel(const SettingsOption *opts, int count,
                               const char *id) {
  int i = optionIndex(opts, count, id);
  if (i < 0) {
    return id ? id : "";
  }
  return opts[i].label;
}

static bool isKnownModel(const char *id) {
  return settingsImageModelIndex(id) >= 0;
}

static void copyBounded(char *dst, size_t dstMax, const char *src) {
  if (!dst || dstMax == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstMax - 1);
  dst[dstMax - 1] = '\0';
}

static bool validDeviceName(const char *name) {
  if (!name || !name[0]) {
    return false;
  }
  size_t n = strlen(name);
  return n >= 1 && n < SETTINGS_NAME_MAX;
}

static bool validWifiSsid(const char *ssid) {
  if (!ssid || !ssid[0]) {
    return false;
  }
  return strlen(ssid) < SETTINGS_NAME_MAX;
}

static bool validApPassword(const char *password) {
  if (!password) {
    return false;
  }
  size_t n = strlen(password);
  // WPA2 SoftAP requires ≥8 chars
  return n >= 8 && n < SETTINGS_AP_PASS_MAX;
}

static void useDefaults() {
  copyBounded(sImageModel, sizeof(sImageModel), IMAGE_MODEL);
  copyBounded(sAspectRatio, sizeof(sAspectRatio), kDefaultAspect);
  copyBounded(sResolution, sizeof(sResolution), kDefaultResolution);
  copyBounded(sDeviceName, sizeof(sDeviceName), kDefaultDeviceName);
  copyBounded(sWifiSsid, sizeof(sWifiSsid), WIFI_SSID);
  copyBounded(sWifiPassword, sizeof(sWifiPassword), WIFI_PASSWORD);
  copyBounded(sApPassword, sizeof(sApPassword), kDefaultApPassword);
  sSaveAudio = false;
  sAiPrompt[0] = '\0';
}

static bool extractJsonString(const String &raw, const char *key, String &out) {
  String needle = String("\"") + key + "\"";
  int keyPos = raw.indexOf(needle);
  if (keyPos < 0) {
    return false;
  }
  int colon = raw.indexOf(':', keyPos + needle.length());
  if (colon < 0) {
    return false;
  }
  int start = raw.indexOf('"', colon + 1);
  if (start < 0) {
    return false;
  }
  start++;
  int end = start;
  while (end < (int)raw.length()) {
    if (raw[end] == '\\' && end + 1 < (int)raw.length()) {
      end += 2;
      continue;
    }
    if (raw[end] == '"') {
      break;
    }
    ++end;
  }
  if (end >= (int)raw.length() || end < start) {
    return false;
  }
  out = "";
  for (int i = start; i < end; ++i) {
    if (raw[i] == '\\' && i + 1 < end) {
      out += raw[i + 1];
      ++i;
    } else {
      out += raw[i];
    }
  }
  return true;
}

static bool extractJsonBool(const String &raw, const char *key, bool &out) {
  String needle = String("\"") + key + "\"";
  int keyPos = raw.indexOf(needle);
  if (keyPos < 0) {
    return false;
  }
  int colon = raw.indexOf(':', keyPos + needle.length());
  if (colon < 0) {
    return false;
  }
  int i = colon + 1;
  while (i < (int)raw.length() &&
         (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n')) {
    ++i;
  }
  if (raw.startsWith("true", i)) {
    out = true;
    return true;
  }
  if (raw.startsWith("false", i)) {
    out = false;
    return true;
  }
  return false;
}

static void appendJsonString(File &f, const char *s) {
  if (!s) {
    return;
  }
  for (const char *p = s; *p; ++p) {
    if (*p == '\\' || *p == '"') {
      f.write('\\');
    }
    f.write((uint8_t)*p);
  }
}

static bool saveToFs() {
  if (!storageBegin()) {
    return false;
  }
  File f = imageFs().open(SETTINGS_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("ERR settings write open");
    return false;
  }
  f.print("{\"image_model\":\"");
  appendJsonString(f, sImageModel);
  f.print("\",\"aspect_ratio\":\"");
  appendJsonString(f, sAspectRatio);
  f.print("\",\"resolution\":\"");
  appendJsonString(f, sResolution);
  f.print("\",\"device_name\":\"");
  appendJsonString(f, sDeviceName);
  f.print("\",\"wifi_ssid\":\"");
  appendJsonString(f, sWifiSsid);
  f.print("\",\"wifi_password\":\"");
  appendJsonString(f, sWifiPassword);
  f.print("\",\"ap_password\":\"");
  appendJsonString(f, sApPassword);
  f.print("\",\"save_audio\":");
  f.print(sSaveAudio ? "true" : "false");
  f.print(",\"ai_prompt\":\"");
  appendJsonString(f, sAiPrompt);
  f.print("\"}\n");
  f.close();
  return true;
}

static bool loadFromFs() {
  if (!storageBegin()) {
    return false;
  }
  if (!imageFs().exists(SETTINGS_PATH)) {
    return false;
  }
  File f = imageFs().open(SETTINGS_PATH, FILE_READ);
  if (!f) {
    return false;
  }
  String raw;
  while (f.available()) {
    raw += (char)f.read();
  }
  f.close();

  bool any = false;
  String val;

  if (extractJsonString(raw, "image_model", val)) {
    if (isKnownModel(val.c_str())) {
      copyBounded(sImageModel, sizeof(sImageModel), val.c_str());
      any = true;
    } else {
      Serial.printf("settings: unknown model \"%s\"\n", val.c_str());
    }
  }

  if (extractJsonString(raw, "aspect_ratio", val)) {
    if (optionIndex(kAspectRatios, kAspectRatioCount, val.c_str()) >= 0) {
      copyBounded(sAspectRatio, sizeof(sAspectRatio), val.c_str());
      any = true;
    } else {
      Serial.printf("settings: unknown aspect_ratio \"%s\"\n", val.c_str());
    }
  }

  if (extractJsonString(raw, "resolution", val)) {
    if (optionIndex(kResolutions, kResolutionCount, val.c_str()) >= 0) {
      copyBounded(sResolution, sizeof(sResolution), val.c_str());
      any = true;
    } else {
      Serial.printf("settings: unknown resolution \"%s\"\n", val.c_str());
    }
  }

  if (extractJsonString(raw, "device_name", val)) {
    if (validDeviceName(val.c_str())) {
      copyBounded(sDeviceName, sizeof(sDeviceName), val.c_str());
      any = true;
    }
  }

  if (extractJsonString(raw, "wifi_ssid", val)) {
    if (validWifiSsid(val.c_str())) {
      copyBounded(sWifiSsid, sizeof(sWifiSsid), val.c_str());
      any = true;
    }
  }

  if (extractJsonString(raw, "wifi_password", val)) {
    if (val.length() < SETTINGS_WIFI_PASS_MAX) {
      copyBounded(sWifiPassword, sizeof(sWifiPassword), val.c_str());
      any = true;
    }
  }

  if (extractJsonString(raw, "ap_password", val)) {
    if (validApPassword(val.c_str())) {
      copyBounded(sApPassword, sizeof(sApPassword), val.c_str());
      any = true;
    }
  }

  bool saveAudio = false;
  if (extractJsonBool(raw, "save_audio", saveAudio)) {
    sSaveAudio = saveAudio;
    any = true;
  }

  if (extractJsonString(raw, "ai_prompt", val)) {
    if (val.length() < SETTINGS_PROMPT_MAX) {
      copyBounded(sAiPrompt, sizeof(sAiPrompt), val.c_str());
      any = true;
    }
  }

  return any;
}

bool settingsBegin() {
  useDefaults();
  if (loadFromFs()) {
    Serial.printf("settings: model=%s aspect=%s res=%s device=%s wifi=%s\n",
                  sImageModel, sAspectRatio, sResolution, sDeviceName,
                  sWifiSsid);
  } else {
    Serial.printf("settings: defaults model=%s aspect=%s res=%s device=%s\n",
                  sImageModel, sAspectRatio, sResolution, sDeviceName);
    saveToFs();
  }
  sReady = true;
  return true;
}

static void ensureReady() {
  if (!sReady) {
    settingsBegin();
  }
}

const char *settingsImageModel() {
  ensureReady();
  return sImageModel;
}

bool settingsSetImageModel(const char *id) {
  if (!id || !id[0] || !isKnownModel(id)) {
    return false;
  }
  ensureReady();
  if (strcmp(sImageModel, id) == 0) {
    return true;
  }
  copyBounded(sImageModel, sizeof(sImageModel), id);
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: image_model=%s\n", sImageModel);
  return true;
}

int settingsImageModelCount() { return kImageModelCount; }

const SettingsImageModel *settingsImageModelAt(int index) {
  if (index < 0 || index >= kImageModelCount) {
    return nullptr;
  }
  return &kImageModels[index];
}

const char *settingsImageModelLabel(const char *id) {
  int i = settingsImageModelIndex(id);
  if (i < 0) {
    return id ? id : "";
  }
  return kImageModels[i].label;
}

int settingsImageModelIndex(const char *id) {
  if (!id) {
    return -1;
  }
  for (int i = 0; i < kImageModelCount; ++i) {
    if (strcmp(kImageModels[i].id, id) == 0) {
      return i;
    }
  }
  return -1;
}

const char *settingsAspectRatio() {
  ensureReady();
  return sAspectRatio;
}

bool settingsSetAspectRatio(const char *id) {
  if (optionIndex(kAspectRatios, kAspectRatioCount, id) < 0) {
    return false;
  }
  ensureReady();
  if (strcmp(sAspectRatio, id) == 0) {
    return true;
  }
  copyBounded(sAspectRatio, sizeof(sAspectRatio), id);
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: aspect_ratio=%s\n", sAspectRatio);
  return true;
}

int settingsAspectRatioCount() { return kAspectRatioCount; }

const SettingsOption *settingsAspectRatioAt(int index) {
  if (index < 0 || index >= kAspectRatioCount) {
    return nullptr;
  }
  return &kAspectRatios[index];
}

const char *settingsAspectRatioLabel(const char *id) {
  return optionLabel(kAspectRatios, kAspectRatioCount, id);
}

int settingsAspectRatioIndex(const char *id) {
  return optionIndex(kAspectRatios, kAspectRatioCount, id);
}

const char *settingsResolution() {
  ensureReady();
  return sResolution;
}

bool settingsSetResolution(const char *id) {
  if (optionIndex(kResolutions, kResolutionCount, id) < 0) {
    return false;
  }
  ensureReady();
  if (strcmp(sResolution, id) == 0) {
    return true;
  }
  copyBounded(sResolution, sizeof(sResolution), id);
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: resolution=%s\n", sResolution);
  return true;
}

int settingsResolutionCount() { return kResolutionCount; }

const SettingsOption *settingsResolutionAt(int index) {
  if (index < 0 || index >= kResolutionCount) {
    return nullptr;
  }
  return &kResolutions[index];
}

const char *settingsResolutionLabel(const char *id) {
  return optionLabel(kResolutions, kResolutionCount, id);
}

int settingsResolutionIndex(const char *id) {
  return optionIndex(kResolutions, kResolutionCount, id);
}

const char *settingsDeviceName() {
  ensureReady();
  return sDeviceName;
}

bool settingsSetDeviceName(const char *name) {
  if (!validDeviceName(name)) {
    return false;
  }
  ensureReady();
  if (strcmp(sDeviceName, name) == 0) {
    return true;
  }
  copyBounded(sDeviceName, sizeof(sDeviceName), name);
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: device_name=%s\n", sDeviceName);
  return true;
}

const char *settingsWifiSsid() {
  ensureReady();
  return sWifiSsid;
}

bool settingsSetWifiSsid(const char *ssid) {
  if (!validWifiSsid(ssid)) {
    return false;
  }
  ensureReady();
  if (strcmp(sWifiSsid, ssid) == 0) {
    return true;
  }
  copyBounded(sWifiSsid, sizeof(sWifiSsid), ssid);
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: wifi_ssid=%s\n", sWifiSsid);
  return true;
}

const char *settingsWifiPassword() {
  ensureReady();
  return sWifiPassword;
}

bool settingsSetWifiPassword(const char *password) {
  if (!password || strlen(password) >= SETTINGS_WIFI_PASS_MAX) {
    return false;
  }
  ensureReady();
  if (strcmp(sWifiPassword, password) == 0) {
    return true;
  }
  copyBounded(sWifiPassword, sizeof(sWifiPassword), password);
  if (!saveToFs()) {
    return false;
  }
  Serial.println("settings: wifi_password updated");
  return true;
}

const char *settingsApPassword() {
  ensureReady();
  return sApPassword;
}

bool settingsSetApPassword(const char *password) {
  if (!validApPassword(password)) {
    return false;
  }
  ensureReady();
  if (strcmp(sApPassword, password) == 0) {
    return true;
  }
  copyBounded(sApPassword, sizeof(sApPassword), password);
  if (!saveToFs()) {
    return false;
  }
  Serial.println("settings: ap_password updated");
  return true;
}

bool settingsSaveAudio() {
  ensureReady();
  return sSaveAudio;
}

bool settingsSetSaveAudio(bool on) {
  ensureReady();
  if (sSaveAudio == on) {
    return true;
  }
  sSaveAudio = on;
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: save_audio=%d\n", (int)sSaveAudio);
  return true;
}

const char *settingsAiPrompt() {
  ensureReady();
  return sAiPrompt;
}

bool settingsSetAiPrompt(const char *text) {
  ensureReady();
  if (!text) text = "";
  if (strlen(text) >= SETTINGS_PROMPT_MAX) {
    return false;
  }
  if (strcmp(sAiPrompt, text) == 0) {
    return true;
  }
  copyBounded(sAiPrompt, sizeof(sAiPrompt), text);
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("settings: ai_prompt=\"%s\"\n", sAiPrompt);
  return true;
}

const char *settingsShareApSsid() { return settingsDeviceName(); }

const char *settingsShareApPassword() { return settingsApPassword(); }
