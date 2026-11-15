#include "fatalError.h"
#include "hookRegistry.h"
#include "moduleWatch.h"
#include "pluginHost.h"
#include "sessionLog.h"

#include <windows.h>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

// The plugin the tests of watchModuleLoads load into the fixture game: it hooks fixtureModule.dll's one export each
// time the module is loaded, so the export answers 40 more than its own value, and takes the hook back each time the
// module is freed. It has no Unload: a watch stays registered.

static const char* const pluginName = "module-probe";
static const char* const probeVersion = "0.1.0";
static const char* const moduleName = "fixtureModule.dll";
static const int addedValue = 40;

typedef int(__cdecl* ModuleValueFunction)();

static bool isLoaded = false;
static void* hookedTarget = nullptr;
static ModuleValueFunction originalModuleValue = nullptr;

static int __cdecl moduleValueDetour() {
  return originalModuleValue() + addedValue;
}

static void hookModule() {
  hookedTarget = resolveLoadedExport(moduleName, "fixtureModuleValue");
  installHook("fixtureModule!fixtureModuleValue", hookedTarget, reinterpret_cast<void*>(&moduleValueDetour),
    reinterpret_cast<void**>(&originalModuleValue));
}

static void unhookModule() {
  removeHook(hookedTarget);
  hookedTarget = nullptr;
  originalModuleValue = nullptr;
}

extern "C" {

__declspec(dllexport) DWORD Load(const PluginHost* host) {
  if (isLoaded) {
    fatalError("Load was called twice");
  }
  isLoaded = true;
  setFatalErrorTitle(pluginName);
  openSessionLog(host->pluginDirectory, pluginName, host->sessionStamp);
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s mainThreadID=%lu", probeVersion, ankhPluginsBuildCommit,
    host->mainThreadID);
  initializeHookRegistry();
  watchModuleLoads(moduleName, &hookModule, &unhookModule);
  writeLogLine("INFO", "loadComplete");
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

}
