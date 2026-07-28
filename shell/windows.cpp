#include "windows.h"
#include "ask_window.h"
#include "gallery_window.h"
#include "make_photo_window.h"
#include "menu_window.h"
#include "profile_picker_window.h"
#include "profiles.h"
#include "profiles_window.h"
#include "settings_window.h"
#include "web_window.h"

static void onHomeSelect(int index, const MenuItem &item) {
  (void)index;
  switch (item.id) {
  case 1:
    gWindows.push(windowProfilePicker(ProfileType::Image));
    break;
  case 2:
    gWindows.push(windowProfilePicker(ProfileType::Text));
    break;
  case 3:
    gWindows.push(windowGallery());
    break;
  case 4:
    gWindows.push(windowWeb());
    break;
  case 5:
    gWindows.push(windowProfiles());
    break;
  case 6:
    gWindows.push(windowSettings());
    break;
  default:
    break;
  }
}

static MenuItem kHomeItems[] = {
    {"Make Photo", 1, "image", false, nullptr},
    {"Ask", 2, "talk", false, nullptr},
    {"Gallery", 3, "gallery", false, nullptr},
    {"Web", 4, "wifi", false, nullptr},
    {"Profiles", 5, "chat", false, nullptr},
    {"Settings", 6, "gear", false, nullptr},
};

static MenuWindow sHome("Home", kHomeItems, 6, onHomeSelect,
                        "Create photos with AI", "home");

Window *windowHome() { return &sHome; }
