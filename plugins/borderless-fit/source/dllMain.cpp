#include "fatalError.h"
#include "hookRegistry.h"
#include "monitorCover.h"
#include "sessionLog.h"
#include "pluginHost.h"

#include <windows.h>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

typedef HWND (WINAPI* CreateWindowExWFunction)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef HWND (WINAPI* CreateWindowExAFunction)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL (WINAPI* SetWindowPosFunction)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL (WINAPI* MoveWindowFunction)(HWND, int, int, int, int, BOOL);

static const char* const pluginName = "borderless-fit";
static const char* const borderlessFitVersion = "0.3.0";

static bool isLoaded = false;
static DWORD gameMainThreadID = 0;
static CreateWindowExWFunction originalCreateWindowExW = nullptr;
static CreateWindowExAFunction originalCreateWindowExA = nullptr;
static SetWindowPosFunction originalSetWindowPos = nullptr;
static MoveWindowFunction originalMoveWindow = nullptr;

struct MonitorSearch {
  RECT requested;
  bool isMonitorRect;
};

static BOOL CALLBACK matchMonitor(HMONITOR, HDC, LPRECT bounds, LPARAM searchAddress) {
  MonitorSearch* search = reinterpret_cast<MonitorSearch*>(searchAddress);
  if (EqualRect(bounds, &search->requested)) {
    search->isMonitorRect = true;
    return FALSE;
  }
  return TRUE;
}

// The main thread's popup windows asking for exactly a monitor's rectangle are the borderless game window, and that
// monitor gets its corner covered.
static void coverGameMonitor(const char* function, DWORD style, HWND parent, int x, int y, int width, int height) {
  if (GetCurrentThreadId() != gameMainThreadID || parent != nullptr || (style & WS_POPUP) == 0 || (style & WS_CHILD) != 0) {
    return;
  }
  MonitorSearch search = { { x, y, x + width, y + height }, false };
  EnumDisplayMonitors(nullptr, nullptr, &matchMonitor, reinterpret_cast<LPARAM>(&search));
  if (search.isMonitorRect) {
    writeLogLine("INFO", "borderlessWindowSeen function=%s x=%d y=%d width=%d height=%d", function, x, y, width, height);
    coverMonitorCorner(search.requested);
  }
}

static HWND WINAPI createWindowExWDetour(DWORD exStyle, LPCWSTR className, LPCWSTR title, DWORD style, int x, int y, int width,
    int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
  coverGameMonitor("CreateWindowExW", style, parent, x, y, width, height);
  return originalCreateWindowExW(exStyle, className, title, style, x, y, width, height, parent, menu, instance, parameter);
}

static HWND WINAPI createWindowExADetour(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width,
    int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
  coverGameMonitor("CreateWindowExA", style, parent, x, y, width, height);
  return originalCreateWindowExA(exStyle, className, title, style, x, y, width, height, parent, menu, instance, parameter);
}

static bool isTopLevel(HWND window) {
  return window != nullptr && GetAncestor(window, GA_ROOT) == window;
}

// SDL resizes without moving (SWP_NOMOVE), so the window's current position stands in for the ignored x and y.
static BOOL WINAPI setWindowPosDetour(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags) {
  if ((flags & SWP_NOSIZE) == 0 && isTopLevel(window)) {
    POINT position = { x, y };
    if ((flags & SWP_NOMOVE) != 0) {
      RECT bounds = {};
      if (!GetWindowRect(window, &bounds)) {
        fatalError("GetWindowRect failed for window %p (error %lu)", static_cast<void*>(window), GetLastError());
      }
      position = { bounds.left, bounds.top };
    }
    coverGameMonitor("SetWindowPos", static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), nullptr, position.x,
      position.y, width, height);
  }
  return originalSetWindowPos(window, insertAfter, x, y, width, height, flags);
}

static BOOL WINAPI moveWindowDetour(HWND window, int x, int y, int width, int height, BOOL repaint) {
  if (isTopLevel(window)) {
    coverGameMonitor("MoveWindow", static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), nullptr, x, y, width, height);
  }
  return originalMoveWindow(window, x, y, width, height, repaint);
}

template <typename Function>
static void hookUser32(const char* exportName, Function detour, Function* original) {
  installExportHook("user32.dll", exportName, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}

extern "C" {

// The loader calls this on the game's main thread at its entry point, before any game code runs and so before the game
// creates its window. The plugin has no Unload, so it loads only at game start. Every failure is fatal.
__declspec(dllexport) DWORD Load(const PluginHost* host) {
  if (isLoaded) {
    fatalError("Load was called twice");
  }
  isLoaded = true;

  setFatalErrorTitle(pluginName);
  openSessionLog(host->pluginDirectory, pluginName, host->sessionStamp);
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s mainThreadID=%lu", borderlessFitVersion, ankhPluginsBuildCommit,
    GetCurrentThreadId());
  gameMainThreadID = host->mainThreadID;
  initializeHookRegistry();
  hookUser32("CreateWindowExW", &createWindowExWDetour, &originalCreateWindowExW);
  hookUser32("CreateWindowExA", &createWindowExADetour, &originalCreateWindowExA);
  hookUser32("SetWindowPos", &setWindowPosDetour, &originalSetWindowPos);
  hookUser32("MoveWindow", &moveWindowDetour, &originalMoveWindow);
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
