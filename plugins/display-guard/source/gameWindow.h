#pragma once

#include "configuration.h"

#include <windows.h>

// The game's main window is the one its Direct3D device presents into. From the device's creation on it is a
// borderless, non-topmost popup covering the configured monitor: the game's own restyling and moving of it becomes
// that placement, the game cannot change the display mode, and it cannot bring itself in front of another
// application's window. The game sees a client area exactly the size of its back buffer: size and mouse messages
// and its client/screen conversions are mapped through the picture the back buffer is presented into.
void installGameWindowHooks(const Configuration& configuration);

// Makes the window the game's main window, with the back buffer of the device just created for it. Fatal for a
// child window, and for a second window while the first still exists.
void adoptGameWindow(HWND window, UINT backBufferWidth, UINT backBufferHeight);

// Records a new back buffer size for the game's main window. Fatal for any other window.
void setGameBackBuffer(HWND window, UINT width, UINT height);

// The game's main window and its back buffer size. False until a device exists.
bool gameBackBuffer(HWND* window, UINT* width, UINT* height);
