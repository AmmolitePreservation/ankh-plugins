#pragma once

#include "hookRegistry.h"

#include <windows.h>

#include <string>
#include <vector>

// The modules whose user32 imports display-guard hooks: the game's executable, and the game's own libraries that
// display-guard.cfg's windowModules names, which make the window calls for a game built on one (SDL2, for one). Set
// once at load, before any hook; fatal when a named library is not loaded by then.
void setWindowModules(const std::vector<std::string>& libraryNames);
const std::vector<HMODULE>& windowModules();

// Hooks a user32 function where those modules import it, with a detour of the function's own type, so the detour sees
// the game's calls and nobody else's. False when none of them imports the function.
template <typename Function>
bool hookUser32(const char* functionName, Function detour, Function* original) {
  bool isHooked = false;
  for (const HMODULE module : windowModules()) {
    if (installImportHook(module, "user32.dll", functionName, reinterpret_cast<void*>(detour),
        reinterpret_cast<void**>(original))) {
      isHooked = true;
    }
  }
  return isHooked;
}
