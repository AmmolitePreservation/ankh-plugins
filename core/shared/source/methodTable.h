#pragma once

#include "hookRegistry.h"

#include <cstddef>

// The function in a slot of a COM object's method table, as a hook target. Objects of one class share the table,
// so a hook on one object's method covers every object of the class.
void* methodOf(void* object, size_t slot);

// installHookOnce on an object's method, with a detour of the original's own type.
template <typename Function>
void hookMethod(const char* methodName, void* object, size_t slot, Function detour, Function* original) {
  installHookOnce(methodName, methodOf(object, slot), reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}
