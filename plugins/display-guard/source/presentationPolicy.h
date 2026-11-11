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
