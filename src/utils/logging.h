#pragma once

#include "utils/config.h"
#include <wx/log.h>

// Format: [LEVEL] message...
// Note: macros forward printf-style args directly to wxLog* functions.

#if DEBUG_MODE
    // Debug messages appear only in debug builds
    #define LOG_DEBUG(...) wxLogMessage("[DEBUG] " __VA_ARGS__)
#else
    // compile-away in release builds
    #define LOG_DEBUG(...) ((void)0)
#endif

// Always-available logs (these will still be captured by the LogWindow when attached)
#define LOG_INFO(...)  wxLogMessage("[INFO] " __VA_ARGS__)
#define LOG_WARN(...)  wxLogWarning("[WARN] " __VA_ARGS__)
#define LOG_ERROR(...) wxLogError("[ERROR] " __VA_ARGS__)
