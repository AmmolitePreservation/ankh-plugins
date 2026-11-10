#include "pluginHost.h"

extern "C" __declspec(dllexport) DWORD PluginInterfaceVersion() {
  return pluginHostInterfaceVersion;
}
