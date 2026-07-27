#include "settings_window.h"

#include "display.h"
#include "gallery.h"
#include "icon.h"
#include "input.h"
#include "settings.h"
#include "ui_clip.h"
#include "ui_text.h"

#include <Adafruit_ILI9341.h>
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
  }
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
    {"Confirm clear", 1, "bin", false},
    {"Cancel", 2, "back", false},
};

static MenuWindow sClearGallery("Clear Gallery", sClearGalleryItems, 2,
                                onClearGallerySelect, nullptr, "gallery");

// ---- Settings root ----

class SettingsWindow : public Window {
public:
  const char *title() const override { return "Settings"; }
  const char *icon() const override { return "gear"; }
  void onEnter() override;
  bool onEvent(JoyEvent e) override;
  int scrollContentHeight() const override;
  void drawContent(int originX, int originY) override;

private:
  int focus_ = 0;

  int rowHeight() const { return 8 * kUiMenuSize + 10 + 12; }
  int rowTop(int index) const { return 8 + index * rowHeight(); }
  void paintRow(int index, bool focused);
  void openRow(int index);
};

static SettingsWindow sSettings;

void SettingsWindow::onEnter() {
  Window::onEnter();
  focus_ = 0;
}

int SettingsWindow::scrollContentHeight() const {
  return 8 + kRowCount * rowHeight() + 8;
}

void SettingsWindow::paintRow(int index, bool focused) {
  reclaimDisplay();
  const int cy = rowTop(index);
  const int rh = rowHeight();
  const int y = toScreenY(cy);
  const int rowW = tft.width() - 2 * kUiPadX;
  uint16_t bg = focused ? 0x3A2A : 0x18C3;
  uint16_t fg = focused ? ILI9341_YELLOW : ILI9341_WHITE;
  uint16_t sub = focused ? 0xC618 : 0x8410;

  int drawY = y;
  int drawH = rh - 2;
  if (drawY < kWinTitleH) {
    drawH -= (kWinTitleH - drawY);
    drawY = kWinTitleH;
  }
  if (drawY + drawH > tft.height()) {
    drawH = tft.height() - drawY;
  }
  if (drawH <= 0) {
    return;
  }

  if (y >= kWinTitleH && y + rh - 2 <= tft.height()) {
    tft.fillRoundRect(kUiPadX, y, rowW, rh - 2, 4, bg);
    if (focused) {
      tft.drawRoundRect(kUiPadX, y, rowW, rh - 2, 4, ILI9341_CYAN);
    }
  } else {
    tft.fillRect(kUiPadX, drawY, rowW, drawH, bg);
  }

  const char *title = "";
  const char *value = "";
  const char *iconId = "image";
  static char shareSub[40];
  switch (index) {
  case kRowModel:
    title = "Image AI Model";
    value = settingsImageModelLabel(settingsImageModel());
    iconId = "image";
    break;
  case kRowAspect:
    title = "Aspect Ratio";
    value = settingsAspectRatioLabel(settingsAspectRatio());
    iconId = "image";
    break;
  case kRowResolution:
    title = "Resolution";
    value = settingsResolutionLabel(settingsResolution());
    iconId = "image";
    break;
  case kRowShare:
    title = "Web Wi-Fi";
    snprintf(shareSub, sizeof(shareSub), "%s / %s", settingsShareApSsid(),
             settingsShareApPassword());
    value = shareSub;
    iconId = "wifi";
    break;
  case kRowSaveAudio:
    title = "Save audio to SD";
    value = settingsSaveAudio() ? "On" : "Off";
    iconId = "storage";
    break;
  case kRowClearGallery:
    title = "Clear Gallery";
    value = "Delete all photos";
    iconId = "bin";
    break;
  default:
    break;
  }

  const int iconPad = Icon::kSize + 8;
  const int textX = kUiPadX + kUiMenuItemPadX + iconPad;
  const int textMaxW = rowW - 2 * kUiMenuItemPadX - iconPad;
  const int iconY = y + 8;
  if (iconY + Icon::kSize > kWinTitleH && iconY < tft.height()) {
    Icon(iconId).draw((int16_t)(kUiPadX + kUiMenuItemPadX), (int16_t)iconY, fg,
                      bg);
  }

  TextStyle st;
  st.size = kUiMenuSize;
  st.color = fg;
  st.flags = TextFlagNoWrap | TextFlagTruncate;
  st.maxLines = 1;
  st.clipTop = kWinTitleH;
  st.clipBottom = tft.height();
  Text::draw(title, (int16_t)textX, (int16_t)(y + 4), (int16_t)textMaxW, st);

  st.size = 1;
  st.color = sub;
  Text::draw(value ? value : "", (int16_t)textX,
             (int16_t)(y + 4 + 8 * kUiMenuSize + 2), (int16_t)textMaxW, st);
}

void SettingsWindow::drawContent(int ox, int oy) {
  (void)ox;
  (void)oy;
  uiClipSet(kWinTitleH, (int16_t)tft.height());
  for (int i = 0; i < kRowCount; ++i) {
    paintRow(i, i == focus_);
  }
  uiClipClear();
}

void SettingsWindow::openRow(int index) {
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
    // SoftAP credentials — informational (edit via Web settings)
    break;
  case kRowSaveAudio:
    settingsSetSaveAudio(!settingsSaveAudio());
    paintRow(kRowSaveAudio, true);
    break;
  case kRowClearGallery:
    gWindows.push(&sClearGallery);
    break;
  default:
    break;
  }
}

bool SettingsWindow::onEvent(JoyEvent e) {
  if (e == JoyEvent::Up) {
    if (focus_ > 0) {
      int prev = focus_;
      --focus_;
      ensureVisible(rowTop(focus_), rowHeight());
      paintRow(prev, false);
      paintRow(focus_, true);
    }
    return true;
  }
  if (e == JoyEvent::Down) {
    if (focus_ < kRowCount - 1) {
      int prev = focus_;
      ++focus_;
      ensureVisible(rowTop(focus_), rowHeight());
      paintRow(prev, false);
      paintRow(focus_, true);
    }
    return true;
  }
  if (e == JoyEvent::Ok) {
    openRow(focus_);
    return true;
  }
  return false;
}

Window *windowSettings() { return &sSettings; }
