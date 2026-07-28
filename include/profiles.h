#pragma once

#include <stddef.h>
#include <stdint.h>

#define PROFILES_PATH "/profiles.json"
#define PROFILES_MAX 12
#define PROFILE_NAME_MAX 32
#define PROFILE_MODEL_MAX 96
#define PROFILE_PROMPT_MAX 512

enum class ProfileType : uint8_t { Image = 0, Text = 1 };

struct Profile {
  uint16_t id = 0;
  char name[PROFILE_NAME_MAX] = {};
  char model[PROFILE_MODEL_MAX] = {};
  char prompt[PROFILE_PROMPT_MAX] = {};
  ProfileType type = ProfileType::Image;
};

struct ProfileModel {
  const char *label;
  const char *id;
};

bool profilesBegin();

int profilesCount();
const Profile *profilesAt(int index);
const Profile *profilesFindId(uint16_t id);

int profilesCountOfType(ProfileType type);
// index among profiles of that type only (0 .. count-1)
const Profile *profilesAtType(ProfileType type, int index);

bool profilesAdd(const Profile &p);          // assigns new id
bool profilesUpdate(const Profile &p);       // by id
bool profilesRemove(uint16_t id);

// Session selection after picker (0 = none).
uint16_t profilesActiveId();
void profilesSetActiveId(uint16_t id);
const Profile *profilesActive();

// Model whitelists by type.
int profilesModelCount(ProfileType type);
const ProfileModel *profilesModelAt(ProfileType type, int index);
const char *profilesModelLabel(ProfileType type, const char *id);
int profilesModelIndex(ProfileType type, const char *id);
bool profilesModelKnown(ProfileType type, const char *id);

const char *profilesDefaultAskPrompt();
