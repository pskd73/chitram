#pragma once

#include <Arduino.h>
#include <stddef.h>

struct TmplVar {
  const char *name;
  const char *value;
};

// Register a named partial for {{import.name}} (call from static init).
void tmplRegisterImport(const char *name, const char *body);

struct TmplImportReg {
  TmplImportReg(const char *name, const char *body);
};

// Replace {{name}} tokens. {{import.x}} inlines registered partials (nested).
// Unknown tokens → empty. Writes into out.
bool tmplRender(const char *tmpl, size_t tmplLen, const TmplVar *vars, int nVars,
                String &out);
