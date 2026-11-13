#include "daoRedirect.h"
#include "fatalError.h"
#include "hookRegistry.h"
#include "pluginHost.h"
#include "sessionLog.h"

#include <windows.h>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

static const char* const pluginName = "database-dao";
static const char* const databaseDAOVersion = "0.1.0";

static bool isLoaded = false;

extern "C" {

// The loader calls this on the game's main thread at its entry point, before any game code runs. The plugin
// has no Unload, so it loads only at game start. Every failure is fatal.
__declspec(dllexport) DWORD Load(const PluginHost* host) {
  if (isLoaded) {
    fatalError("Load was called twice");
  }
  isLoaded = true;

  setFatalErrorTitle(pluginName);
  openSessionLog(host->pluginDirectory, pluginName, host->sessionStamp);
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s", databaseDAOVersion, ankhPluginsBuildCommit);
  initializeHookRegistry();
  installDAORedirect();
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
