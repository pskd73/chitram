#pragma once

// SoftAP + HTTP server for the device dashboard.
bool apServerStart();
void apServerStop();
bool apServerActive();
void apServerPoll();

const char *apServerUrl();
const char *apServerSsid();
const char *apServerPassword();
const char *apServerStatus();

// Live WebServer for page handlers (valid while active).
class WebServer;
WebServer *apServerInstance();
