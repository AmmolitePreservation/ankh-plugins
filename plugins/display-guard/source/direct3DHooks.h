#pragma once

#include "configuration.h"

// Makes every Direct3D 9 device the game creates a windowed one presenting into its main window, which becomes the
// borderless game window, and shows each frame in the picture the presentation policy gives. A swap chain for any
// other window is fatal.
void installDirect3DHooks(const Configuration& configuration);
