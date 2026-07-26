#include "windows.h"
#include "ask_window.h"
#include "gallery_window.h"
#include "settings_window.h"
#include "web_window.h"

static void onHomeSelect(int index, const MenuItem &item) {
  (void)index;
  switch (item.id) {
  case 1:
    gWindows.push(windowAsk());
    break;
  case 2:
    gWindows.push(windowGallery());
    break;
  case 3:
    gWindows.push(windowWeb());
    break;
  case 4:
    gWindows.push(windowSettings());
    break;
  default:
    break;
  }
}

static MenuItem kHomeItems[] = {
    {"Ask", 1, "chat", false},
    {"Gallery", 2, "gallery", false},
    {"Web", 3, "wifi", false},
    {"Settings", 4, "gear", false},
};

static MenuWindow sHome("Home", kHomeItems, 4, onHomeSelect,
                        "Create photos with AI", "home");

Window *windowHome() { return &sHome; }
