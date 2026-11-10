#pragma once

// Names the plugin in the fatal error message box. Call first thing in Load.
void setFatalErrorTitle(const char* pluginName);

// Records the message in the session log (when one is open), shows it in a
// message box, and terminates the process. Never returns.
[[noreturn]] void fatalError(const char* format, ...);
