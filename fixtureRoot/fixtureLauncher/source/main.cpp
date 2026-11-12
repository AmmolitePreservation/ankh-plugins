#include <windows.h>

#include <cstdlib>

// A stand-in launcher for the tests. It imports msimg32.dll, as the launchers launcher-skip stands in for do, so
// launcher-skip beside it starts with it and runs the game in its place. Exit code 3: its own code ran, so nothing
// skipped it.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // Never called: naming the function is what makes the executable import msimg32.dll.
  if (__argc > 1000) {
    const BLENDFUNCTION blend = {};
    AlphaBlend(nullptr, 0, 0, 0, 0, nullptr, 0, 0, 0, 0, blend);
  }
  return 3;
}
