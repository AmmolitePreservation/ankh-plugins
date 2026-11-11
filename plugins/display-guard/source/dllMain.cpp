#include "configuration.h"
#include "direct3DHooks.h"
#include "fatalError.h"
#include "gameWindow.h"
#include "hookRegistry.h"
#include "inputHooks.h"
#include "presentationPolicy.h"
#include "sessionLog.h"
#include "windowPlacement.h"
#include "pluginHost.h"

#include <windows.h>

#include <string>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

static const char* const pluginName = "display-guard";
static const char* const displayGuardVersion = "0.1.0";

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
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s mainThreadID=%lu", displayGuardVersion, ankhPluginsBuildCommit,
    GetCurrentThreadId());

  const Configuration configuration = loadConfiguration(pluginDirectory);
  writeLogLine("INFO", "configurationLoaded monitor=%s renderResolution=%s startMinimized=%s releaseInput=%s",
    configuration.monitor.c_str(), configuration.renderResolution == RenderResolution::native ? "native" : "game",
    configuration.startMinimized ? "true" : "false", configuration.releaseInput ? "true" : "false");
  const RECT target = monitorRectangle(configuration.monitor);
  writeLogLine("INFO", "monitorSelected monitor=%s left=%ld top=%ld width=%ld height=%ld", configuration.monitor.c_str(),
    target.left, target.top, target.right - target.left, target.bottom - target.top);

  initializeHookRegistry();
  initializePresentationPolicy(host);
  installGameWindowHooks(configuration);
  installDirect3DHooks(configuration);
  installInputHooks(configuration);
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
