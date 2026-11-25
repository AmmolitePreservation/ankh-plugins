#include "user32Hook.h"

#include "fatalError.h"

static std::vector<HMODULE> hookedModules;

void setWindowModules(const std::vector<std::string>& libraryNames) {
  if (!hookedModules.empty()) {
    fatalError("setWindowModules called twice");
  }
  hookedModules.push_back(GetModuleHandleA(nullptr));
  for (const std::string& name : libraryNames) {
    const HMODULE module = GetModuleHandleA(name.c_str());
    if (module == nullptr) {
      fatalError("display-guard.cfg's windowModules names %s, which the game has not loaded when display-guard starts",
        name.c_str());
    }
    hookedModules.push_back(module);
  }
}

const std::vector<HMODULE>& windowModules() {
  if (hookedModules.empty()) {
    fatalError("A window hook was asked for before setWindowModules");
  }
  return hookedModules;
}
