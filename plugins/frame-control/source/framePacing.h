#pragma once

#include "configuration.h"

// Holds the game's Direct3D 9 presents to the configured frame rates by waiting before each one. With both
// rates unlimited nothing is hooked. initializeHookRegistry comes first.
void installFramePacing(const Configuration& configuration);
