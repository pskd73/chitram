#include "settings_window.h"

#include "gallery.h"
#include "menu_window.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

enum SettingsRow : int {
  kRowModel = 0,
  kRowAspect = 1,
  kRowResolution = 2,
  kRowShare = 3,
  kRowSaveAudio = 4,
  kRowClearGallery = 5,
  kRowCount = 6,
};

static const int kModelCap = 8;
static const int kOptionCap = 8;
static MenuItem sModelItems[kModelCap];
static MenuItem sAspectItems[kOptionCap];
static MenuItem sResItems[kOptionCap];
static int sModelCount = 0;
static int sAspectCount = 0;
static int sResCount = 0;

static char sModelSub[40];
static char sAspectSub[24];
static char sResSub[16];
static char sShareSub[40];
static char sSaveAudioSub[8];

static MenuItem sSettingsItems[kRowCount];
static MenuWindow *sSettingsWindow = nullptr;

static void syncModelItems() {
  sModelCount = settingsImageModelCount();
  if (sModelCount > kModelCap) {
    sModelCount = kModelCap;
  }
  const char *cur = settingsImageModel();
  for (int i = 0; i < sModelCount; ++i) {
    const SettingsImageModel *m = settingsImageModelAt(i);
    sModelItems[i].label = m ? m->label : "?";
    sModelItems[i].id = i;
    sModelItems[i].icon = nullptr;
    sModelItems[i].selected = m && cur && strcmp(m->id, cur) == 0;
    sModelItems[i].subtitle = nullptr;
  }
}

static void syncAspectItems() {
  sAspectCount = settingsAspectRatioCount();
  if (sAspectCount > kOptionCap) {
    sAspectCount = kOptionCap;
  }
  const char *cur = settingsAspectRatio();
  for (int i = 0; i < sAspectCount; ++i) {
    const SettingsOption *o = settingsAspectRatioAt(i);
    sAspectItems[i].label = o ? o->label : "?";
    sAspectItems[i].id = i;
    sAspectItems[i].icon = nullptr;
    sAspectItems[i].selected = o && cur && strcmp(o->id, cur) == 0;
    sAspectItems[i].subtitle = nullptr;
  }
}

static void syncResItems() {
  sResCount = settingsResolutionCount();
  if (sResCount > kOptionCap) {
    sResCount = kOptionCap;
  }
  const char *cur = settingsResolution();
  for (int i = 0; i < sResCount; ++i) {
    const SettingsOption *o = settingsResolutionAt(i);
    sResItems[i].label = o ? o->label : "?";
    sResItems[i].id = i;
    sResItems[i].icon = nullptr;
    sResItems[i].selected = o && cur && strcmp(o->id, cur) == 0;
    sResItems[i].subtitle = nullptr;
  }
}

static void syncSettingsItems() {
  const char *model = settingsImageModelLabel(settingsImageModel());
  const char *aspect = settingsAspectRatioLabel(settingsAspectRatio());
  const char *res = settingsResolutionLabel(settingsResolution());
  snprintf(sModelSub, sizeof(sModelSub), "%s", model ? model : "");
  snprintf(sAspectSub, sizeof(sAspectSub), "%s", aspect ? aspect : "");
  snprintf(sResSub, sizeof(sResSub), "%s", res ? res : "");
  snprintf(sShareSub, sizeof(sShareSub), "%s / %s", settingsShareApSsid(),
           settingsShareApPassword());
  snprintf(sSaveAudioSub, sizeof(sSaveAudioSub), "%s",
           settingsSaveAudio() ? "On" : "Off");

  sSettingsItems[kRowModel].label = "Image AI Model";
  sSettingsItems[kRowModel].id = kRowModel;
  sSettingsItems[kRowModel].icon = "image";
  sSettingsItems[kRowModel].selected = false;
  sSettingsItems[kRowModel].subtitle = sModelSub;

  sSettingsItems[kRowAspect].label = "Aspect Ratio";
  sSettingsItems[kRowAspect].id = kRowAspect;
  sSettingsItems[kRowAspect].icon = "image";
  sSettingsItems[kRowAspect].selected = false;
  sSettingsItems[kRowAspect].subtitle = sAspectSub;

  sSettingsItems[kRowResolution].label = "Resolution";
  sSettingsItems[kRowResolution].id = kRowResolution;
  sSettingsItems[kRowResolution].icon = "image";
  sSettingsItems[kRowResolution].selected = false;
  sSettingsItems[kRowResolution].subtitle = sResSub;

  sSettingsItems[kRowShare].label = "Web Wi-Fi";
  sSettingsItems[kRowShare].id = kRowShare;
  sSettingsItems[kRowShare].icon = "wifi";
  sSettingsItems[kRowShare].selected = false;
  sSettingsItems[kRowShare].subtitle = sShareSub;

  sSettingsItems[kRowSaveAudio].label = "Save audio to SD";
  sSettingsItems[kRowSaveAudio].id = kRowSaveAudio;
  sSettingsItems[kRowSaveAudio].icon = "storage";
  sSettingsItems[kRowSaveAudio].selected = false;
  sSettingsItems[kRowSaveAudio].subtitle = sSaveAudioSub;

  sSettingsItems[kRowClearGallery].label = "Clear Gallery";
  sSettingsItems[kRowClearGallery].id = kRowClearGallery;
  sSettingsItems[kRowClearGallery].icon = "bin";
  sSettingsItems[kRowClearGallery].selected = false;
  sSettingsItems[kRowClearGallery].subtitle = "Delete all photos";
}

static void onModelSelect(int index, const MenuItem &item) {
  (void)item;
  const SettingsImageModel *m = settingsImageModelAt(index);
  if (!m) {
    return;
  }
  settingsSetImageModel(m->id);
  syncModelItems();
  gWindows.pop();
}

static void onAspectSelect(int index, const MenuItem &item) {
  (void)item;
  const SettingsOption *o = settingsAspectRatioAt(index);
  if (!o) {
    return;
  }
  settingsSetAspectRatio(o->id);
  syncAspectItems();
  gWindows.pop();
}

static void onResSelect(int index, const MenuItem &item) {
  (void)item;
  const SettingsOption *o = settingsResolutionAt(index);
  if (!o) {
    return;
  }
  settingsSetResolution(o->id);
  syncResItems();
  gWindows.pop();
}

class ModelPickerWindow : public MenuWindow {
public:
  ModelPickerWindow()
      : MenuWindow("Image AI Model", sModelItems, settingsImageModelCount(),
                   onModelSelect, nullptr, "image") {}

  void onEnter() override {
    Window::onEnter();
    syncModelItems();
    menu().setItems(sModelItems, sModelCount);
    int idx = settingsImageModelIndex(settingsImageModel());
    setFocusedIndex(idx >= 0 ? idx : 0);
  }
};

class AspectPickerWindow : public MenuWindow {
public:
  AspectPickerWindow()
      : MenuWindow("Aspect Ratio", sAspectItems, settingsAspectRatioCount(),
                   onAspectSelect, nullptr, "image") {}

  void onEnter() override {
    Window::onEnter();
    syncAspectItems();
    menu().setItems(sAspectItems, sAspectCount);
    int idx = settingsAspectRatioIndex(settingsAspectRatio());
    setFocusedIndex(idx >= 0 ? idx : 0);
  }
};

class ResolutionPickerWindow : public MenuWindow {
public:
  ResolutionPickerWindow()
      : MenuWindow("Resolution", sResItems, settingsResolutionCount(),
                   onResSelect, nullptr, "image") {}

  void onEnter() override {
    Window::onEnter();
    syncResItems();
    menu().setItems(sResItems, sResCount);
    int idx = settingsResolutionIndex(settingsResolution());
    setFocusedIndex(idx >= 0 ? idx : 0);
  }
};

static ModelPickerWindow sModelPicker;
static AspectPickerWindow sAspectPicker;
static ResolutionPickerWindow sResPicker;

static void onClearGallerySelect(int index, const MenuItem &item) {
  (void)index;
  if (item.id == 1) {
    int n = galleryClearAll();
    Serial.printf("settings: clear gallery result=%d\n", n);
  }
  gWindows.pop();
}

static MenuItem sClearGalleryItems[] = {
    {"Confirm clear", 1, "bin", false, nullptr},
    {"Cancel", 2, "back", false, nullptr},
};

static MenuWindow sClearGallery("Clear Gallery", sClearGalleryItems, 2,
                                onClearGallerySelect, nullptr, "gallery");

static void onSettingsSelect(int index, const MenuItem &item) {
  (void)item;
  switch (index) {
  case kRowModel:
    syncModelItems();
    gWindows.push(&sModelPicker);
    break;
  case kRowAspect:
    syncAspectItems();
    gWindows.push(&sAspectPicker);
    break;
  case kRowResolution:
    syncResItems();
    gWindows.push(&sResPicker);
    break;
  case kRowShare:
    break;
  case kRowSaveAudio:
    settingsSetSaveAudio(!settingsSaveAudio());
    syncSettingsItems();
    if (sSettingsWindow) {
      sSettingsWindow->drawContentArea();
    }
    break;
  case kRowClearGallery:
    gWindows.push(&sClearGallery);
    break;
  default:
    break;
  }
}

class SettingsWindow : public MenuWindow {
public:
  SettingsWindow()
      : MenuWindow("Settings", sSettingsItems, kRowCount, onSettingsSelect,
                   nullptr, "gear") {
    menu().setWrapNavigation(false);
    sSettingsWindow = this;
  }

  void onEnter() override {
    Window::onEnter();
    syncSettingsItems();
    menu().setItems(sSettingsItems, kRowCount);
    menu().resetFocus();
  }

  void onFocus() override { syncSettingsItems(); }
};

static SettingsWindow sSettings;

Window *windowSettings() { return &sSettings; }
