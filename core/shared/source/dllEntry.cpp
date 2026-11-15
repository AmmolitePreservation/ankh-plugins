#include <windows.h>

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE instance, DWORD reason, LPVOID reserved);
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved);

// Every DLL built here enters here (the build sets /ENTRY). As the process ends, Windows tells each DLL it is detaching
// while the game's window procedure and the hooks still call into the plugins: the C runtime's teardown would destroy
// their globals under them, and a plugin would then read an empty setting or lose its log. Windows' guidance is to clean
// up nothing then, so the plugin's own DllMain does its exit work (totals, closing capture files) and the runtime's
// teardown, which would follow it, is skipped. A DLL freed while the process goes on, a plugin's reload, tears down.
extern "C" BOOL WINAPI dllEntry(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
    return DllMain(instance, reason, reserved);
  }
  return _DllMainCRTStartup(instance, reason, reserved);
}
