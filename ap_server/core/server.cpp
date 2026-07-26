#include "server.h"

#include "registry.h"
#include "settings.h"

#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

static WebServer sServer(80);
static char sUrl[40] = {};
static char sStatus[48] = "off";
static bool sActive = false;
static bool sRoutesBound = false;

WebServer *apServerInstance() { return sActive ? &sServer : nullptr; }

bool apServerStart() {
  apServerStop();

  const char *ssid = settingsShareApSsid();
  const char *pass = settingsShareApPassword();

  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, pass)) {
    strncpy(sStatus, "AP fail", sizeof(sStatus) - 1);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  IPAddress ip = WiFi.softAPIP();
  snprintf(sUrl, sizeof(sUrl), "http://%u.%u.%u.%u/", ip[0], ip[1], ip[2],
           ip[3]);

  if (!sRoutesBound) {
    apRegistryApply(sServer);
    sServer.onNotFound([]() {
      sServer.sendHeader("Location", "/", true);
      sServer.send(302, "text/plain", "");
    });
    sRoutesBound = true;
  }

  sServer.begin();
  sActive = true;
  strncpy(sStatus, "online", sizeof(sStatus) - 1);
  Serial.printf("ap_server: AP \"%s\" %s routes ready\n", ssid, sUrl);
  return true;
}

void apServerStop() {
  if (sActive) {
    sServer.stop();
    sActive = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  sUrl[0] = '\0';
  strncpy(sStatus, "off", sizeof(sStatus) - 1);
}

bool apServerActive() { return sActive; }

void apServerPoll() {
  if (sActive) {
    sServer.handleClient();
  }
}

const char *apServerUrl() { return sUrl[0] ? sUrl : "http://192.168.4.1/"; }

const char *apServerSsid() { return settingsShareApSsid(); }

const char *apServerPassword() { return settingsShareApPassword(); }

const char *apServerStatus() { return sStatus; }
