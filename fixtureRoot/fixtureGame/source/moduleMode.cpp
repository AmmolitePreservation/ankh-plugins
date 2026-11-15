#include "moduleMode.h"

#include "fixtureReport.h"

#include <windows.h>

#include <string>

static const char* const moduleFileName = "fixtureModule.dll";
static const int cycleCount = 2;

typedef int(__cdecl* ModuleValueFunction)();

static std::string modulePath() {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return std::string();
  }
  std::string folder(path, length);
  return folder.substr(0, folder.find_last_of('\\') + 1) + moduleFileName;
}

int runModuleMode() {
  if (!openReport()) {
    return 8;
  }
  const std::string path = modulePath();
  if (path.empty()) {
    failStep("modulePath", GetLastError());
    return 9;
  }
  for (int cycle = 1; cycle <= cycleCount; cycle++) {
    const HMODULE module = LoadLibraryA(path.c_str());
    if (module == nullptr) {
      failStep("loadModule", GetLastError());
      return 9;
    }
    const ModuleValueFunction moduleValue = reinterpret_cast<ModuleValueFunction>(GetProcAddress(module, "fixtureModuleValue"));
    if (moduleValue == nullptr) {
      failStep("moduleExport", GetLastError());
      return 9;
    }
    writeReport("moduleValue cycle=%d value=%d", cycle, moduleValue());
    if (!FreeLibrary(module)) {
      failStep("freeModule", GetLastError());
      return 9;
    }
    writeReport("moduleFreed cycle=%d isLoaded=%d", cycle, GetModuleHandleA(moduleFileName) != nullptr ? 1 : 0);
  }
  writeReport("ready");
  return 0;
}
