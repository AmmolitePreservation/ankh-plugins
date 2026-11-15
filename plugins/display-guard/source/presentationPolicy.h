#pragma once

#include "pluginHost.h"

#include <windows.h>

// Where in the game's window its back buffer is shown. The default keeps the back buffer's shape; a game whose
// picture needs another placement replaces handlers/presentationPolicy.cpp.

// Called once from Load, after the hook registry is ready and before the plugin installs its own hooks.
void initializePresentationPolicy(const PluginHost* host);

// The rectangle inside target that a back buffer of this size is presented into. Called on the game's threads for
// every frame and every mouse message.
RECT presentationRectangle(UINT backBufferWidth, UINT backBufferHeight, const RECT& target);

// The size of the back buffer the game renders at with renderResolution native, in a window of this size: by default the
// window's own. A game whose picture has another shape for a while gives the largest picture of that shape the window
// holds, so it is shown pixel for pixel; when the size changes, display-guard has the game resize its device the way a
// resized window does. Called on the game's threads for every frame.
SIZE nativeBackBufferSize(LONG windowWidth, LONG windowHeight);
