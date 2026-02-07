#pragma once

#include "config.h"
#include <wx/log.h>

// Use wxLogMessage in debug; compile away in non-debug.
#if DEBUG_MODE
    // Forward any args to wxLogMessage; wxWidgets accepts printf-like args or wxString
    #define LOG_DEBUG(...) wxLogMessage(__VA_ARGS__)
#else
    // No-op that compiles away
    #define LOG_DEBUG(...) ((void)0)
#endif
