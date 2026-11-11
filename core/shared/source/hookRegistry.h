#pragma once

#include <windows.h>

// Owns MinHook initialization and remembers every hooked address.
void initializeHookRegistry();

// Looks up an export in a module that is already loaded. Fatal when missing.
void* resolveLoadedExport(const char* moduleName, const char* exportName);

// Installs and enables an inline hook. Two names can resolve to one address
// when the compiler folds identical functions; the first hook already covers
// that code, so the second name is logged as shared and false is returned.
bool installHook(const char* targetName, void* target, void* detour, void** original);

// installHook on an export of a loaded module, named <module>!<export> with the module's name lacking ".dll".
bool installExportHook(const char* moduleName, const char* exportName, void* detour, void** original);

// installHook for a target that later calls name again, as each new object of a class names its methods: the first
// call installs the hook and a later one with the same target does nothing. A later one with another target is
// fatal, since that code would run unhooked.
void installHookOnce(const char* targetName, void* target, void* detour, void** original);

// Points importingModule's import of functionName from importedModuleName at detour and gives what it pointed at
// as original. Only calls importingModule makes through its import table reach the detour: not the plugin's own,
// another plugin's or a system library's, so the hooks of several plugins chain in any order. False, with nothing
// changed, when the module does not import the function. Fatal for imports by ordinal, without names or
// delay-loaded.
bool installImportHook(HMODULE importingModule, const char* importedModuleName, const char* functionName, void* detour,
  void** original);

// Overwrites size bytes of the game's code at address with replacement once the bytes there equal
// expected; anything else there is fatal. Logged and registered like a hook.
void patchCode(const char* patchName, void* address, const unsigned char* expected, const unsigned char* replacement, size_t size);

// Points the 5-byte relative call at callSite to newTarget once it calls expectedTarget; fatal otherwise.
void redirectCall(const char* patchName, void* callSite, void* expectedTarget, void* newTarget);
