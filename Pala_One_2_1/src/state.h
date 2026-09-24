#ifndef PALA_STATE_H
#define PALA_STATE_H

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>

#include "src/config.h"
#include "src/hal/epd.h"            // EInkDisplay
#include "src/pure/paginator.h"     // LayoutMetrics

// Use LittleFS as the project's filesystem. Defined AFTER FS.h has declared
// `class FS`, so the macro doesn't collide with that class name.
#define FS LittleFS

// ============================================================================
//  Globals (definitions live in state.cpp)
// ============================================================================
extern WebServer server;
extern Preferences prefs;

extern char AP_SSID[24];
extern const char* AP_PASS;

extern EInkDisplay display;

#endif  // PALA_STATE_H
