#include "windows.h"
#include "ask_window.h"
#include "gallery_window.h"
#include "make_photo_window.h"
#include "menu_window.h"
#include "settings_window.h"
#include "web_window.h"

static void onHomeSelect(int index, const MenuItem &item) {
  (void)index;
  switch (item.id) {
  case 1:
    gWindows.push(windowMakePhoto());
    break;
  case 2:
    gWindows.push(windowAsk());
    break;
  case 3:
    gWindows.push(windowGallery());
    break;
  case 4:
    gWindows.push(windowWeb());
    break;
  case 5:
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
    {"Settings", 5, "gear", false, nullptr},
};

static MenuWindow sHome("Home", kHomeItems, 5, onHomeSelect,
                        "Create photos with AI", "home");

Window *windowHome() { return &sHome; }
