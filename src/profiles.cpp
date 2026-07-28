#include "profiles.h"

#include "config.h"
#include "settings.h"
#include "storage.h"

#include <Arduino.h>
#include <string.h>

static const char *kDefaultAskPrompt =
    "You are a helpful voice assistant on a small handheld device. "
    "Answer clearly in plain text. No markdown, no bullet lists. "
    "Be concise for simple questions. When the user asks for a long "
    "story or detailed answer, write the full response without cutting off.";

static const ProfileModel kTextModels[] = {
    {"GPT 5 Nano", "openai/gpt-5-nano"},
    {"GPT 4.1 Mini", "openai/gpt-4.1-mini"},
    {"Gemini 2.5 Flash", "google/gemini-2.5-flash"},
};
static const int kTextModelCount =
    (int)(sizeof(kTextModels) / sizeof(kTextModels[0]));

static Profile sProfiles[PROFILES_MAX];
static int sCount = 0;
static uint16_t sNextId = 1;
static uint16_t sActiveId = 0;
static bool sReady = false;

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

static bool extractJsonString(const String &raw, int from, int to,
                              const char *key, String &out) {
  String needle = String("\"") + key + "\"";
  int keyPos = raw.indexOf(needle, from);
  if (keyPos < 0 || keyPos >= to) {
    return false;
  }
  int colon = raw.indexOf(':', keyPos + needle.length());
  if (colon < 0 || colon >= to) {
    return false;
  }
  int start = raw.indexOf('"', colon + 1);
  if (start < 0 || start >= to) {
    return false;
  }
  start++;
  int end = start;
  while (end < to) {
    if (raw[end] == '\\' && end + 1 < to) {
      end += 2;
      continue;
    }
    if (raw[end] == '"') {
      break;
    }
    ++end;
  }
  if (end >= to || end < start) {
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

static bool extractJsonUInt(const String &raw, int from, int to, const char *key,
                            uint16_t &out) {
  String needle = String("\"") + key + "\"";
  int keyPos = raw.indexOf(needle, from);
  if (keyPos < 0 || keyPos >= to) {
    return false;
  }
  int colon = raw.indexOf(':', keyPos + needle.length());
  if (colon < 0 || colon >= to) {
    return false;
  }
  int i = colon + 1;
  while (i < to && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n')) {
    ++i;
  }
  if (i >= to || raw[i] < '0' || raw[i] > '9') {
    return false;
  }
  uint32_t v = 0;
  while (i < to && raw[i] >= '0' && raw[i] <= '9') {
    v = v * 10 + (uint32_t)(raw[i] - '0');
    if (v > 65535) {
      return false;
    }
    ++i;
  }
  out = (uint16_t)v;
  return true;
}

static bool saveToFs() {
  if (!storageBegin()) {
    return false;
  }
  File f = imageFs().open(PROFILES_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("ERR profiles write open");
    return false;
  }
  f.print("{\"next_id\":");
  f.print(sNextId);
  f.print(",\"profiles\":[");
  for (int i = 0; i < sCount; ++i) {
    if (i) {
      f.print(',');
    }
    const Profile &p = sProfiles[i];
    f.print("{\"id\":");
    f.print(p.id);
    f.print(",\"name\":\"");
    appendJsonString(f, p.name);
    f.print("\",\"type\":\"");
    f.print(p.type == ProfileType::Text ? "text" : "image");
    f.print("\",\"model\":\"");
    appendJsonString(f, p.model);
    f.print("\",\"prompt\":\"");
    appendJsonString(f, p.prompt);
    f.print("\"}");
  }
  f.print("]}\n");
  f.close();
  return true;
}

static void seedDefaults() {
  sCount = 0;
  sNextId = 1;

  Profile img = {};
  img.id = sNextId++;
  copyBounded(img.name, sizeof(img.name), "Default Photo");
  img.type = ProfileType::Image;
  const char *im = settingsImageModel();
  copyBounded(img.model, sizeof(img.model),
              im && im[0] ? im : IMAGE_MODEL);
  const char *ap = settingsAiPrompt();
  copyBounded(img.prompt, sizeof(img.prompt), ap ? ap : "");
  sProfiles[sCount++] = img;

  Profile txt = {};
  txt.id = sNextId++;
  copyBounded(txt.name, sizeof(txt.name), "Assistant");
  txt.type = ProfileType::Text;
  copyBounded(txt.model, sizeof(txt.model), CHAT_MODEL);
  copyBounded(txt.prompt, sizeof(txt.prompt), kDefaultAskPrompt);
  sProfiles[sCount++] = txt;
}

static bool loadFromFs() {
  if (!storageBegin()) {
    return false;
  }
  if (!imageFs().exists(PROFILES_PATH)) {
    return false;
  }
  File f = imageFs().open(PROFILES_PATH, FILE_READ);
  if (!f) {
    return false;
  }
  String raw = f.readString();
  f.close();
  if (raw.length() < 8) {
    return false;
  }

  sCount = 0;
  uint16_t nextId = 1;
  extractJsonUInt(raw, 0, (int)raw.length(), "next_id", nextId);

  int arr = raw.indexOf("\"profiles\"");
  if (arr < 0) {
    return false;
  }
  int lb = raw.indexOf('[', arr);
  if (lb < 0) {
    return false;
  }
  int pos = lb + 1;
  const int end = (int)raw.length();
  while (pos < end && sCount < PROFILES_MAX) {
    int objStart = raw.indexOf('{', pos);
    if (objStart < 0) {
      break;
    }
    int objEnd = raw.indexOf('}', objStart);
    if (objEnd < 0) {
      break;
    }
    // Handle nested braces in strings roughly: find matching } not in string
    // Simple approach: scan with string awareness
    {
      bool inStr = false;
      int depth = 0;
      objEnd = -1;
      for (int i = objStart; i < end; ++i) {
        char c = raw[i];
        if (inStr) {
          if (c == '\\' && i + 1 < end) {
            ++i;
            continue;
          }
          if (c == '"') {
            inStr = false;
          }
          continue;
        }
        if (c == '"') {
          inStr = true;
          continue;
        }
        if (c == '{') {
          ++depth;
        } else if (c == '}') {
          --depth;
          if (depth == 0) {
            objEnd = i;
            break;
          }
        }
      }
      if (objEnd < 0) {
        break;
      }
    }

    Profile p = {};
    uint16_t id = 0;
    String name, type, model, prompt;
    if (!extractJsonUInt(raw, objStart, objEnd + 1, "id", id)) {
      pos = objEnd + 1;
      continue;
    }
    extractJsonString(raw, objStart, objEnd + 1, "name", name);
    extractJsonString(raw, objStart, objEnd + 1, "type", type);
    extractJsonString(raw, objStart, objEnd + 1, "model", model);
    extractJsonString(raw, objStart, objEnd + 1, "prompt", prompt);

    p.id = id;
    copyBounded(p.name, sizeof(p.name), name.c_str());
    p.type = (type == "text") ? ProfileType::Text : ProfileType::Image;
    copyBounded(p.model, sizeof(p.model), model.c_str());
    copyBounded(p.prompt, sizeof(p.prompt), prompt.c_str());

    if (!p.name[0]) {
      copyBounded(p.name, sizeof(p.name), "Profile");
    }
    if (!profilesModelKnown(p.type, p.model)) {
      copyBounded(p.model, sizeof(p.model),
                  p.type == ProfileType::Text ? CHAT_MODEL : IMAGE_MODEL);
    }
    sProfiles[sCount++] = p;
    if (id >= nextId) {
      nextId = (uint16_t)(id + 1);
    }
    pos = objEnd + 1;
  }

  sNextId = nextId ? nextId : 1;
  return sCount > 0;
}

bool profilesBegin() {
  if (sReady) {
    return true;
  }
  settingsBegin(); // migrate image_model / ai_prompt into seed
  if (!loadFromFs()) {
    seedDefaults();
    saveToFs();
    Serial.printf("profiles: seeded %d\n", sCount);
  } else {
    Serial.printf("profiles: loaded %d next_id=%u\n", sCount,
                  (unsigned)sNextId);
  }
  sReady = true;
  return true;
}

static void ensureReady() {
  if (!sReady) {
    profilesBegin();
  }
}

int profilesCount() {
  ensureReady();
  return sCount;
}

const Profile *profilesAt(int index) {
  ensureReady();
  if (index < 0 || index >= sCount) {
    return nullptr;
  }
  return &sProfiles[index];
}

const Profile *profilesFindId(uint16_t id) {
  ensureReady();
  if (!id) {
    return nullptr;
  }
  for (int i = 0; i < sCount; ++i) {
    if (sProfiles[i].id == id) {
      return &sProfiles[i];
    }
  }
  return nullptr;
}

int profilesCountOfType(ProfileType type) {
  ensureReady();
  int n = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sProfiles[i].type == type) {
      ++n;
    }
  }
  return n;
}

const Profile *profilesAtType(ProfileType type, int index) {
  ensureReady();
  if (index < 0) {
    return nullptr;
  }
  int seen = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sProfiles[i].type != type) {
      continue;
    }
    if (seen == index) {
      return &sProfiles[i];
    }
    ++seen;
  }
  return nullptr;
}

bool profilesAdd(const Profile &p) {
  ensureReady();
  if (sCount >= PROFILES_MAX) {
    return false;
  }
  if (!p.name[0] || !profilesModelKnown(p.type, p.model)) {
    return false;
  }
  Profile &dst = sProfiles[sCount];
  dst = p;
  dst.id = sNextId++;
  if (strlen(dst.prompt) >= PROFILE_PROMPT_MAX) {
    dst.prompt[PROFILE_PROMPT_MAX - 1] = '\0';
  }
  ++sCount;
  if (!saveToFs()) {
    --sCount;
    return false;
  }
  Serial.printf("profiles: add id=%u name=%s type=%s\n", (unsigned)dst.id,
                dst.name, dst.type == ProfileType::Text ? "text" : "image");
  return true;
}

bool profilesUpdate(const Profile &p) {
  ensureReady();
  if (!p.id || !p.name[0] || !profilesModelKnown(p.type, p.model)) {
    return false;
  }
  for (int i = 0; i < sCount; ++i) {
    if (sProfiles[i].id != p.id) {
      continue;
    }
    ProfileType oldType = sProfiles[i].type;
    sProfiles[i] = p;
    sProfiles[i].prompt[PROFILE_PROMPT_MAX - 1] = '\0';
    // Type changes allowed only if still valid model for new type
    (void)oldType;
    if (!saveToFs()) {
      return false;
    }
    Serial.printf("profiles: update id=%u\n", (unsigned)p.id);
    return true;
  }
  return false;
}

bool profilesRemove(uint16_t id) {
  ensureReady();
  if (!id) {
    return false;
  }
  // Keep at least one profile overall
  if (sCount <= 1) {
    return false;
  }
  int idx = -1;
  for (int i = 0; i < sCount; ++i) {
    if (sProfiles[i].id == id) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    return false;
  }
  // Don't remove the last profile of a type if it would leave zero of that type
  // — actually plan allows delete; picker can show empty. Allow delete.
  for (int i = idx; i < sCount - 1; ++i) {
    sProfiles[i] = sProfiles[i + 1];
  }
  --sCount;
  if (sActiveId == id) {
    sActiveId = 0;
  }
  if (!saveToFs()) {
    return false;
  }
  Serial.printf("profiles: remove id=%u\n", (unsigned)id);
  return true;
}

uint16_t profilesActiveId() { return sActiveId; }

void profilesSetActiveId(uint16_t id) { sActiveId = id; }

const Profile *profilesActive() { return profilesFindId(sActiveId); }

int profilesModelCount(ProfileType type) {
  if (type == ProfileType::Text) {
    return kTextModelCount;
  }
  return settingsImageModelCount();
}

const ProfileModel *profilesModelAt(ProfileType type, int index) {
  if (type == ProfileType::Text) {
    if (index < 0 || index >= kTextModelCount) {
      return nullptr;
    }
    return &kTextModels[index];
  }
  const SettingsImageModel *m = settingsImageModelAt(index);
  if (!m) {
    return nullptr;
  }
  // SettingsImageModel layout matches ProfileModel (label, id)
  return reinterpret_cast<const ProfileModel *>(m);
}

const char *profilesModelLabel(ProfileType type, const char *id) {
  int i = profilesModelIndex(type, id);
  if (i < 0) {
    return id ? id : "";
  }
  const ProfileModel *m = profilesModelAt(type, i);
  return m ? m->label : (id ? id : "");
}

int profilesModelIndex(ProfileType type, const char *id) {
  if (!id) {
    return -1;
  }
  int n = profilesModelCount(type);
  for (int i = 0; i < n; ++i) {
    const ProfileModel *m = profilesModelAt(type, i);
    if (m && m->id && strcmp(m->id, id) == 0) {
      return i;
    }
  }
  return -1;
}

bool profilesModelKnown(ProfileType type, const char *id) {
  return profilesModelIndex(type, id) >= 0;
}

const char *profilesDefaultAskPrompt() { return kDefaultAskPrompt; }
