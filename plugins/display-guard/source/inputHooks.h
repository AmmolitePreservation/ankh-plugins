#pragma once

#include "configuration.h"

#include <windows.h>

// With releaseInput, keeps the game from holding the keyboard and mouse: its DirectInput 8 devices become
// non-exclusive and foreground-only, and while another application's window is the active one the game's calls to
// move or confine the cursor do nothing and its asynchronous key reads see no key down. With confineCursor, the cursor
// stays inside the game's window while the game is in front, and the game's own release of it confines it there again.
void installInputHooks(const Configuration& configuration);

// Called by the game window's procedure when the game is activated or deactivated, and when its window is placed while
// active. With confineCursor, confines the cursor to the window, or releases the confinement it set.
void updateCursorConfinement(HWND window, bool isActive);
