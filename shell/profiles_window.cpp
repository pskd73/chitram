#include "profiles_window.h"

#include "menu_window.h"
#include "profiles.h"
#include "stt_input_window.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

static const int kListCap = PROFILES_MAX + 1; // + Add
static MenuItem sListItems[kListCap];
static char sListSubs[PROFILES_MAX][48];
static int sListCount = 0;

static Profile sDraft = {};
static bool sDraftIsNew = false;
static bool sTypeLocked = false; // editing existing: type fixed

enum EditRow : int {
  kEditName = 0,
  kEditType = 1,
  kEditModel = 2,
  kEditPrompt = 3,
  kEditSave = 4,
  kEditDelete = 5,
  kEditCount = 6,
};

static MenuItem sEditItems[kEditCount];
static char sNameSub[PROFILE_NAME_MAX + 4];
static char sTypeSub[12];
static char sModelSub[40];
static char sPromptSub[48];

static MenuWindow *sListWindow = nullptr;
static MenuWindow *sEditWindow = nullptr;

static const int kModelCap = 8;
static MenuItem sModelItems[kModelCap];
static int sModelCount = 0;

static void openEditor(bool isNew, const Profile *src);

static void syncListItems() {
  profilesBegin();
  const int n = profilesCount();
  sListCount = 0;
  for (int i = 0; i < n && sListCount < PROFILES_MAX; ++i) {
    const Profile *p = profilesAt(i);
    if (!p) {
      continue;
    }
    sListItems[sListCount].label = p->name;
    sListItems[sListCount].id = (int)p->id;
    sListItems[sListCount].icon =
        p->type == ProfileType::Text ? "chat" : "image";
    sListItems[sListCount].selected = false;
    snprintf(sListSubs[sListCount], sizeof(sListSubs[sListCount]), "%s · %s",
             p->type == ProfileType::Text ? "Text" : "Image",
             profilesModelLabel(p->type, p->model));
    sListItems[sListCount].subtitle = sListSubs[sListCount];
    ++sListCount;
  }
  if (n < PROFILES_MAX) {
    sListItems[sListCount].label = "Add profile";
    sListItems[sListCount].id = -1;
    sListItems[sListCount].icon = "check";
    sListItems[sListCount].selected = false;
    sListItems[sListCount].subtitle = "Create new";
    ++sListCount;
  }
}

static void syncEditItems() {
  snprintf(sNameSub, sizeof(sNameSub), "%s",
           sDraft.name[0] ? sDraft.name : "(unnamed)");
  snprintf(sTypeSub, sizeof(sTypeSub), "%s",
           sDraft.type == ProfileType::Text ? "Text" : "Image");
  snprintf(sModelSub, sizeof(sModelSub), "%s",
           profilesModelLabel(sDraft.type, sDraft.model));
  if (sDraft.prompt[0]) {
    snprintf(sPromptSub, sizeof(sPromptSub), "%.40s", sDraft.prompt);
  } else {
    snprintf(sPromptSub, sizeof(sPromptSub), "None");
  }

  sEditItems[kEditName] = {"Name", kEditName, "chat", false, sNameSub};
  sEditItems[kEditType] = {"Type", kEditType, "gear", false, sTypeSub};
  sEditItems[kEditModel] = {"AI Model", kEditModel, "image", false, sModelSub};
  sEditItems[kEditPrompt] = {"Prompt", kEditPrompt, "chat", false, sPromptSub};
  sEditItems[kEditSave] = {"Save", kEditSave, "check", false, nullptr};
  sEditItems[kEditDelete] = {"Delete", kEditDelete, "bin", false,
                             sDraftIsNew ? "(new)" : nullptr};
}

static void syncModelItems() {
  sModelCount = profilesModelCount(sDraft.type);
  if (sModelCount > kModelCap) {
    sModelCount = kModelCap;
  }
  for (int i = 0; i < sModelCount; ++i) {
    const ProfileModel *m = profilesModelAt(sDraft.type, i);
    sModelItems[i].label = m ? m->label : "?";
    sModelItems[i].id = i;
    sModelItems[i].icon = nullptr;
    sModelItems[i].selected =
        m && sDraft.model[0] && strcmp(m->id, sDraft.model) == 0;
    sModelItems[i].subtitle = nullptr;
  }
}

static void refreshEditUi() {
  syncEditItems();
  if (sEditWindow) {
    sEditWindow->menu().setItems(sEditItems, kEditCount);
    sEditWindow->drawContentArea();
  }
}

static void onModelPick(int index, const MenuItem &item) {
  (void)item;
  const ProfileModel *m = profilesModelAt(sDraft.type, index);
  if (m && m->id) {
    strncpy(sDraft.model, m->id, sizeof(sDraft.model) - 1);
    sDraft.model[sizeof(sDraft.model) - 1] = '\0';
  }
  gWindows.pop();
  refreshEditUi();
}

class ProfileModelPicker : public MenuWindow {
public:
  ProfileModelPicker()
      : MenuWindow("AI Model", sModelItems, 0, onModelPick, nullptr, "image") {}

  void onEnter() override {
    Window::onEnter();
    syncModelItems();
    menu().setItems(sModelItems, sModelCount);
    int idx = profilesModelIndex(sDraft.type, sDraft.model);
    setFocusedIndex(idx >= 0 ? idx : 0);
  }
};

static ProfileModelPicker sModelPicker;

static MenuItem sTypeItems[] = {
    {"Image", 0, "image", false, nullptr},
    {"Text", 1, "chat", false, nullptr},
};

static void onTypePick(int index, const MenuItem &item) {
  (void)item;
  ProfileType t = (index == 1) ? ProfileType::Text : ProfileType::Image;
  if (t != sDraft.type) {
    sDraft.type = t;
    const ProfileModel *m = profilesModelAt(t, 0);
    if (m && m->id) {
      strncpy(sDraft.model, m->id, sizeof(sDraft.model) - 1);
      sDraft.model[sizeof(sDraft.model) - 1] = '\0';
    }
    if (t == ProfileType::Text && !sDraft.prompt[0]) {
      strncpy(sDraft.prompt, profilesDefaultAskPrompt(),
              sizeof(sDraft.prompt) - 1);
      sDraft.prompt[sizeof(sDraft.prompt) - 1] = '\0';
    }
  }
  gWindows.pop();
  refreshEditUi();
}

static MenuWindow sTypePicker("Type", sTypeItems, 2, onTypePick, nullptr,
                              "gear");

static MenuItem sDeleteItems[] = {
    {"Confirm delete", 1, "bin", false, nullptr},
    {"Cancel", 2, "back", false, nullptr},
};

static void onDeleteConfirm(int index, const MenuItem &item) {
  (void)index;
  if (item.id == 1 && !sDraftIsNew) {
    profilesRemove(sDraft.id);
    gWindows.popN(2); // confirm + editor
    if (sListWindow) {
      syncListItems();
      sListWindow->menu().setItems(sListItems, sListCount);
    }
    return;
  }
  gWindows.pop();
}

static MenuWindow sDeleteConfirm("Delete profile", sDeleteItems, 2,
                                 onDeleteConfirm, nullptr, "bin");

static void onEditSelect(int index, const MenuItem &item) {
  (void)item;
  switch (index) {
  case kEditName:
    gWindows.push(windowSttInput(
        "Name", "chat",
        [](void * /*ctx*/, const char *text) {
          if (text && text[0]) {
            strncpy(sDraft.name, text, sizeof(sDraft.name) - 1);
            sDraft.name[sizeof(sDraft.name) - 1] = '\0';
          }
          refreshEditUi();
        },
        nullptr, nullptr));
    break;
  case kEditType:
    if (!sTypeLocked) {
      gWindows.push(&sTypePicker);
    }
    break;
  case kEditModel:
    syncModelItems();
    gWindows.push(&sModelPicker);
    break;
  case kEditPrompt: {
    SttInputOpts opts;
    opts.confirmLabel = "Use prompt";
    gWindows.push(windowSttInput(
        "Prompt", "chat",
        [](void * /*ctx*/, const char *text) {
          if (text) {
            strncpy(sDraft.prompt, text, sizeof(sDraft.prompt) - 1);
            sDraft.prompt[sizeof(sDraft.prompt) - 1] = '\0';
          }
          refreshEditUi();
        },
        nullptr, nullptr, opts));
    break;
  }
  case kEditSave:
    if (!sDraft.name[0]) {
      break;
    }
    if (!profilesModelKnown(sDraft.type, sDraft.model)) {
      break;
    }
    if (sDraftIsNew) {
      if (!profilesAdd(sDraft)) {
        break;
      }
    } else {
      if (!profilesUpdate(sDraft)) {
        break;
      }
    }
    gWindows.pop();
    if (sListWindow) {
      syncListItems();
      sListWindow->menu().setItems(sListItems, sListCount);
    }
    break;
  case kEditDelete:
    if (sDraftIsNew) {
      gWindows.pop();
    } else if (profilesCount() > 1) {
      gWindows.push(&sDeleteConfirm);
    }
    break;
  default:
    break;
  }
}

class ProfileEditWindow : public MenuWindow {
public:
  ProfileEditWindow()
      : MenuWindow("Edit profile", sEditItems, kEditCount, onEditSelect,
                   nullptr, "gear") {
    sEditWindow = this;
  }

  const char *title() const override {
    return sDraftIsNew ? "New profile" : "Edit profile";
  }

  void onEnter() override {
    Window::onEnter();
    syncEditItems();
    menu().setItems(sEditItems, kEditCount);
    menu().resetFocus();
  }

  void onFocus() override { refreshEditUi(); }
};

static ProfileEditWindow sEditor;

static void openEditor(bool isNew, const Profile *src) {
  sDraftIsNew = isNew;
  sTypeLocked = !isNew;
  memset(&sDraft, 0, sizeof(sDraft));
  if (src) {
    sDraft = *src;
  } else {
    sDraft.type = ProfileType::Image;
    const ProfileModel *m = profilesModelAt(ProfileType::Image, 0);
    if (m && m->id) {
      strncpy(sDraft.model, m->id, sizeof(sDraft.model) - 1);
    }
    strncpy(sDraft.name, "New profile", sizeof(sDraft.name) - 1);
  }
  gWindows.push(&sEditor);
}

static void onListSelect(int index, const MenuItem &item) {
  (void)index;
  if (item.id < 0) {
    openEditor(true, nullptr);
    return;
  }
  const Profile *p = profilesFindId((uint16_t)item.id);
  if (p) {
    openEditor(false, p);
  }
}

class ProfilesListWindow : public MenuWindow {
public:
  ProfilesListWindow()
      : MenuWindow("Profiles", sListItems, 0, onListSelect,
                   "Select to edit", "chat") {
    sListWindow = this;
  }

  void onEnter() override {
    Window::onEnter();
    profilesBegin();
    syncListItems();
    menu().setItems(sListItems, sListCount);
    menu().resetFocus();
  }

  void onFocus() override {
    syncListItems();
    menu().setItems(sListItems, sListCount);
  }
};

static ProfilesListWindow sProfilesList;

Window *windowProfiles() { return &sProfilesList; }
