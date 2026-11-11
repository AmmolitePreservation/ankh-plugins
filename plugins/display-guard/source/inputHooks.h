#pragma once

#include "configuration.h"

// With releaseInput, keeps the game from holding the keyboard and mouse: its DirectInput 8 devices become
// non-exclusive and foreground-only, and while another application's window is the active one the game's calls to
// move or confine the cursor do nothing and its asynchronous key reads see no key down.
void installInputHooks(const Configuration& configuration);
