#include "hookRegistry.h"
#include "pluginHost.h"

extern "C" __declspec(dllexport) DWORD PluginInterfaceVersion() {
  return pluginHostInterfaceVersion;
}

// The loader calls it when the plugin's Load returns, before any game code runs.
extern "C" __declspec(dllexport) DWORD CompleteLoad() {
  applyQueuedHooks();
  return 0;
}
