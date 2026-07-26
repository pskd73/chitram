#include "template.h"

#include <string.h>

static const int kImportMax = 16;
static const int kImportDepthMax = 4;

struct ImportSlot {
  const char *name;
  const char *body;
};

static ImportSlot sImports[kImportMax];
static int sImportCount = 0;

void tmplRegisterImport(const char *name, const char *body) {
  if (!name || !name[0] || !body || sImportCount >= kImportMax) {
    return;
  }
  for (int i = 0; i < sImportCount; ++i) {
    if (strcmp(sImports[i].name, name) == 0) {
      sImports[i].body = body;
      return;
    }
  }
  sImports[sImportCount].name = name;
  sImports[sImportCount].body = body;
  ++sImportCount;
}

TmplImportReg::TmplImportReg(const char *name, const char *body) {
  tmplRegisterImport(name, body);
}

static const char *findImport(const char *name, size_t nameLen) {
  for (int i = 0; i < sImportCount; ++i) {
    if (!sImports[i].name) {
      continue;
    }
    size_t n = strlen(sImports[i].name);
    if (n == nameLen && strncmp(sImports[i].name, name, nameLen) == 0) {
      return sImports[i].body;
    }
  }
  return nullptr;
}

static const char *findVar(const TmplVar *vars, int nVars, const char *name,
                           size_t nameLen) {
  for (int i = 0; i < nVars; ++i) {
    if (!vars[i].name) {
      continue;
    }
    size_t n = strlen(vars[i].name);
    if (n == nameLen && strncmp(vars[i].name, name, nameLen) == 0) {
      return vars[i].value ? vars[i].value : "";
    }
  }
  return "";
}

static bool tmplRenderDepth(const char *tmpl, size_t tmplLen, const TmplVar *vars,
                            int nVars, String &out, int depth) {
  out = "";
  if (!tmpl || tmplLen == 0) {
    return true;
  }
  out.reserve(tmplLen + 64);
  size_t i = 0;
  while (i < tmplLen) {
    if (tmpl[i] == '{' && i + 1 < tmplLen && tmpl[i + 1] == '{') {
      size_t j = i + 2;
      while (j + 1 < tmplLen && !(tmpl[j] == '}' && tmpl[j + 1] == '}')) {
        ++j;
      }
      if (j + 1 >= tmplLen) {
        out.concat(tmpl + i);
        break;
      }
      const char *name = tmpl + i + 2;
      size_t nameLen = j - (i + 2);
      while (nameLen > 0 && (*name == ' ' || *name == '\t')) {
        ++name;
        --nameLen;
      }
      while (nameLen > 0 &&
             (name[nameLen - 1] == ' ' || name[nameLen - 1] == '\t')) {
        --nameLen;
      }

      static const char kImportPrefix[] = "import.";
      const size_t kImportPrefixLen = sizeof(kImportPrefix) - 1;
      if (nameLen > kImportPrefixLen &&
          strncmp(name, kImportPrefix, kImportPrefixLen) == 0) {
        if (depth >= kImportDepthMax) {
          i = j + 2;
          continue;
        }
        const char *impName = name + kImportPrefixLen;
        size_t impLen = nameLen - kImportPrefixLen;
        const char *body = findImport(impName, impLen);
        if (body) {
          String nested;
          if (!tmplRenderDepth(body, strlen(body), vars, nVars, nested,
                               depth + 1)) {
            return false;
          }
          out += nested;
        }
      } else {
        out += findVar(vars, nVars, name, nameLen);
      }
      i = j + 2;
      continue;
    }
    out += tmpl[i];
    ++i;
  }
  return true;
}

bool tmplRender(const char *tmpl, size_t tmplLen, const TmplVar *vars, int nVars,
                String &out) {
  return tmplRenderDepth(tmpl, tmplLen, vars, nVars, out, 0);
}
