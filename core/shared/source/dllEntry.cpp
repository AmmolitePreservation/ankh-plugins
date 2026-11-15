#include <windows.h>

extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE instance, DWORD reason, LPVOID reserved);

// Every DLL built here enters here (the build sets /ENTRY). As the process ends, Windows tells each DLL it is detaching
// while the game's window procedure and the hooks still call into the plugins: the C runtime's teardown would destroy
// their globals under them, and a plugin would then read an empty setting or lose its log. Windows' guidance is to clean
// up nothing then, so only a DLL freed while the process goes on, a plugin's reload, runs the teardown.
extern "C" BOOL WINAPI dllEntry(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
    return TRUE;
  }
  return _DllMainCRTStartup(instance, reason, reserved);
}
