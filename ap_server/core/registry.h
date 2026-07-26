#pragma once

#include "template.h"

#include <WebServer.h>

using ApHandler = void (*)(WebServer &server);

struct ApGetReg {
  const char *path;
  ApHandler handler;
};

struct ApPostReg {
  const char *path;
  ApHandler handler;
};

// Append a GET/POST route (call from static initializers in pages/*.cpp).
void apRegisterGet(const char *path, ApHandler handler);
void apRegisterPost(const char *path, ApHandler handler);

// Register several GET routes at once.
struct ApAutoRegister {
  ApAutoRegister(const ApGetReg *regs, int count);
};

struct ApAutoRegisterPost {
  ApAutoRegisterPost(const ApPostReg *regs, int count);
};

// Apply all registered routes onto the live WebServer (once per start).
void apRegistryApply(WebServer &server);

// Send a literal body (HTML/CSS/text).
bool apSendBody(WebServer &server, const char *contentType, const char *body);

// Render {{tokens}} / {{import.name}} then send as text/html.
bool apSendTemplate(WebServer &server, const char *tmpl, const TmplVar *vars,
                    int nVars);

// 303 redirect (PRG after POST).
void apRedirect(WebServer &server, const char *location);
