#pragma once

#include "hookRegistry.h"

#include <windows.h>

// Hooks a user32 function where the game's executable imports it, with a detour of the function's own type, so the
// detour sees the game's calls and nobody else's. False when the executable does not import the function.
template <typename Function>
bool hookUser32(const char* functionName, Function detour, Function* original) {
  return installImportHook(GetModuleHandleA(nullptr), "user32.dll", functionName, reinterpret_cast<void*>(detour),
    reinterpret_cast<void**>(original));
}
