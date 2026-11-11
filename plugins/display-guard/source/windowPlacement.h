#pragma once

#include <windows.h>

#include <string>

// The rectangle, in virtual-screen coordinates, of the monitor named in the settings: "primary" or a display name
// such as DISPLAY2. A name no monitor has is fatal, listing the ones there are.
RECT monitorRectangle(const std::string& monitor);

// The style and extended style of a borderless, non-topmost popup, keeping the other bits given.
LONG borderlessStyle(LONG style);
LONG borderlessExtendedStyle(LONG extendedStyle);

// Makes the window a borderless, non-topmost popup exactly covering the monitor, unless it already is. Returns
// true when it had to change something.
bool placeBorderless(HWND window, const std::string& monitor);

// The largest rectangle with the source's aspect ratio that fits, centered, inside the target.
RECT letterboxRectangle(unsigned long sourceWidth, unsigned long sourceHeight, const RECT& target);
