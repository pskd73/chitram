#include "profile_picker_window.h"

#include "ask_window.h"
#include "make_photo_window.h"
#include "menu_window.h"
#include "profiles.h"
#include "ui_text.h"
#include "window.h"

#include <Adafruit_ILI9341.h>
#include <stdio.h>
#include <string.h>

static const int kPickerCap = PROFILES_MAX;
static MenuItem sPickerItems[kPickerCap];
static char sPickerSubs[kPickerCap][40];
static int sPickerCount = 0;
static ProfileType sPickerType = ProfileType::Image;

static void syncPickerItems() {
  sPickerCount = profilesCountOfType(sPickerType);
  if (sPickerCount > kPickerCap) {
    sPickerCount = kPickerCap;
  }
  for (int i = 0; i < sPickerCount; ++i) {
    const Profile *p = profilesAtType(sPickerType, i);
    sPickerItems[i].label = p ? p->name : "?";
    sPickerItems[i].id = p ? (int)p->id : 0;
    sPickerItems[i].icon =
        sPickerType == ProfileType::Text ? "chat" : "image";
    sPickerItems[i].selected = false;
    if (p) {
      const char *lab = profilesModelLabel(sPickerType, p->model);
      snprintf(sPickerSubs[i], sizeof(sPickerSubs[i]), "%s",
               lab ? lab : p->model);
      sPickerItems[i].subtitle = sPickerSubs[i];
    } else {
      sPickerItems[i].subtitle = nullptr;
    }
  }
}

static void onPickerSelect(int index, const MenuItem &item) {
  (void)index;
  if (!item.id) {
    return;
  }
  profilesSetActiveId((uint16_t)item.id);
  if (sPickerType == ProfileType::Text) {
    gWindows.replaceTop(windowAsk());
  } else {
    gWindows.replaceTop(windowMakePhoto());
  }
}

class ProfilePickerWindow : public MenuWindow {
public:
  ProfilePickerWindow()
      : MenuWindow("Profiles", sPickerItems, 0, onPickerSelect, nullptr,
                   "chat") {}

  const char *title() const override {
    return sPickerType == ProfileType::Text ? "Ask profile" : "Photo profile";
  }
  const char *icon() const override {
    return sPickerType == ProfileType::Text ? "chat" : "image";
  }

  void onEnter() override {
    Window::onEnter();
    profilesBegin();
    syncPickerItems();
    menu().setItems(sPickerItems, sPickerCount);
    menu().resetFocus();
  }

  void drawContent(int ox, int oy) override {
    if (sPickerCount <= 0) {
      textDrawCenteredHint("no profiles yet", contentTop(), kUiBodySize,
                           ILI9341_DARKGREY);
      return;
    }
    MenuWindow::drawContent(ox, oy);
  }
};

static ProfilePickerWindow sPicker;

Window *windowProfilePicker(ProfileType type) {
  sPickerType = type;
  return &sPicker;
}
