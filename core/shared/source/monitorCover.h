#pragma once

#include <windows.h>

// Some drivers hand a window that covers its monitor exactly the screen itself, as they do a fullscreen game, and switch
// an HDR monitor in and out of HDR as it gains and loses the foreground. They do so only while nothing else shows on
// that monitor, so a one-pixel window of the plugin's own sits over the corner of the monitor the game covers: nearly
// transparent, above other windows, never activated, and passing clicks through. Its own thread keeps it.

// Shows the cover at the bottom-right pixel of monitor, once; later calls do nothing. Fatal when it cannot.
void coverMonitorCorner(const RECT& monitor);
