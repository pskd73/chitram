#include "registry.h"

#include <string.h>

static const int kApRouteMax = 32;

struct ApRouteSlot {
  const char *path;
  ApHandler handler;
  HTTPMethod method;
};

static ApRouteSlot sRoutes[kApRouteMax];
static int sRouteCount = 0;
static WebServer *sActive = nullptr;

static void apRegister(const char *path, HTTPMethod method, ApHandler handler) {
  if (!path || !handler || sRouteCount >= kApRouteMax) {
    return;
  }
  for (int i = 0; i < sRouteCount; ++i) {
    if (sRoutes[i].method == method && strcmp(sRoutes[i].path, path) == 0) {
      sRoutes[i].handler = handler;
      return;
    }
  }
  sRoutes[sRouteCount].path = path;
  sRoutes[sRouteCount].handler = handler;
  sRoutes[sRouteCount].method = method;
  ++sRouteCount;
}

void apRegisterGet(const char *path, ApHandler handler) {
  apRegister(path, HTTP_GET, handler);
}

void apRegisterPost(const char *path, ApHandler handler) {
  apRegister(path, HTTP_POST, handler);
}

ApAutoRegister::ApAutoRegister(const ApGetReg *regs, int count) {
  if (!regs || count <= 0) {
    return;
  }
  for (int i = 0; i < count; ++i) {
    apRegisterGet(regs[i].path, regs[i].handler);
  }
}

ApAutoRegisterPost::ApAutoRegisterPost(const ApPostReg *regs, int count) {
  if (!regs || count <= 0) {
    return;
  }
  for (int i = 0; i < count; ++i) {
    apRegisterPost(regs[i].path, regs[i].handler);
  }
}

void apRegistryApply(WebServer &server) {
  sActive = &server;
  for (int i = 0; i < sRouteCount; ++i) {
    const int idx = i;
    server.on(sRoutes[idx].path, sRoutes[idx].method, [idx]() {
      if (sActive && sRoutes[idx].handler) {
        sRoutes[idx].handler(*sActive);
      }
    });
  }
}

bool apSendBody(WebServer &server, const char *contentType, const char *body) {
  if (!body) {
    server.send(500, "text/plain", "empty body");
    return false;
  }
  server.send(200, contentType ? contentType : "text/plain", body);
  return true;
}

bool apSendTemplate(WebServer &server, const char *tmpl, const TmplVar *vars,
                    int nVars) {
  if (!tmpl) {
    server.send(500, "text/plain", "empty template");
    return false;
  }
  String out;
  if (!tmplRender(tmpl, strlen(tmpl), vars, nVars, out)) {
    server.send(500, "text/plain", "template error");
    return false;
  }
  server.send(200, "text/html; charset=utf-8", out);
  return true;
}

void apRedirect(WebServer &server, const char *location) {
  server.sendHeader("Location", location ? location : "/", true);
  server.send(303, "text/plain", "");
}
