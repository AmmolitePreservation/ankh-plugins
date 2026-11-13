#include "configuration.h"
#include "fatalError.h"
#include "pluginHost.h"
#include "sessionLog.h"
#include "workingSetTrim.h"

#include <windows.h>

#include <string>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

static const char* const pluginName = "memory-trim";
static const char* const memoryTrimVersion = "0.1.0";

static bool isLoaded = false;

extern "C" {

// The loader calls this on the game's main thread at its entry point, before any game code runs,
// outside the loader lock. The plugin has no Unload, so it loads only at game start. Every failure is fatal.
__declspec(dllexport) DWORD Load(const PluginHost* host) {
  if (isLoaded) {
    fatalError("Load was called twice");
  }
  isLoaded = true;

  setFatalErrorTitle(pluginName);
  const std::string pluginDirectory = host->pluginDirectory;
  openSessionLog(pluginDirectory, pluginName, host->sessionStamp);
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s mainThreadID=%lu", memoryTrimVersion, ankhPluginsBuildCommit,
    GetCurrentThreadId());

  const Configuration configuration = loadConfiguration(pluginDirectory);
  writeLogLine("INFO", "configurationLoaded backgroundTrimSeconds=%lu foregroundTrimSeconds=%lu",
    configuration.backgroundTrimSeconds, configuration.foregroundTrimSeconds);

  installWorkingSetTrim(configuration, host);
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
