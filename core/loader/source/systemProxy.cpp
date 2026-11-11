// The game imports the system DLL game.cfg's loader= names, and Windows looks for it beside the executable first, so
// this DLL is loaded under that name. Every export the real one has is reproduced (the generated proxyStubs.asm on x64,
// below on x86) as a jump through a pointer to the system copy.
#include "systemProxy.h"

#include "fatalError.h"
#include "proxyExports.h"

#include <windows.h>

#include <string>

// An export the system copy does not have (Wine omits some) resolves to this,
// so calling it fails loudly instead of jumping to address zero.
#define declareMissingExport(exportName) \
  extern "C" void missing##exportName() { \
    fatalError("The system " proxyLibraryName " does not export " #exportName ", but the game called it"); \
  }
proxyExports(declareMissingExport)

// The pointers the stubs jump through.
#define declarePointer(exportName) \
  void* real##exportName = reinterpret_cast<void*>(&missing##exportName);
extern "C" {
proxyExports(declarePointer)
}

#if defined(_M_IX86)
#define declareStub(exportName) \
  extern "C" __declspec(naked) void proxy##exportName() { \
    __asm { jmp dword ptr [real##exportName] } \
  }
proxyExports(declareStub)
#endif

void loadSystemProxyLibrary() {
  char systemDirectory[MAX_PATH];
  const UINT systemDirectoryLength = GetSystemDirectoryA(systemDirectory, sizeof(systemDirectory));
  if (systemDirectoryLength == 0 || systemDirectoryLength >= sizeof(systemDirectory)) {
    fatalError("Could not find the Windows system directory (error %lu)", GetLastError());
  }
  // A full path, so the loader picks the system copy rather than this DLL again.
  const std::string systemLibraryPath = std::string(systemDirectory) + "\\" proxyLibraryName;
  HMODULE systemLibrary = LoadLibraryA(systemLibraryPath.c_str());
  if (systemLibrary == nullptr) {
    fatalError("Could not load %s (error %lu)", systemLibraryPath.c_str(), GetLastError());
  }

#define resolvePointer(exportName) \
  if (FARPROC resolved = GetProcAddress(systemLibrary, #exportName)) { \
    real##exportName = reinterpret_cast<void*>(resolved); \
  }
  proxyExports(resolvePointer)
#undef resolvePointer
}
