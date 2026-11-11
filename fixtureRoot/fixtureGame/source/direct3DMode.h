#pragma once

#include <windows.h>

// The fixture acts as a Direct3D 9 game, for the tests of plugins that change what a game sees of its window,
// device, cursor and keyboard. Arguments: "windowed <width> <height>" or "fullscreen". Each call it makes is one
// "event key=value ..." line in fixtureReport.txt beside the executable; the last is "ready", after which the
// window reports the sizes and mouse positions its procedure is sent. Returns 0, or the process's exit code.
int runDirect3DMode(HINSTANCE instance, int argumentCount, char** arguments);
