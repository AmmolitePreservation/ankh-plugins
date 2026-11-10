#include <windows.h>

#include <cstring>

// A stand-in game for the loader's tests. It imports version.dll, so the loader beside it starts with it, and pumps
// messages on its main thread until a plugin posts WM_QUIT.
// Exit codes: the WM_QUIT's (0 from the test plugin); 2 the system folder did not resolve; 3 nothing ended it within
// its lifetime; 4 the version.dll call the loader forwards failed; 5 the timer did not start; 6 GetMessage failed.

static const DWORD lifetimeMilliseconds = 120000;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  static const char libraryName[] = "\\kernel32.dll";
  char libraryPath[MAX_PATH];
  const UINT systemFolderLength = GetSystemDirectoryA(libraryPath, sizeof(libraryPath));
  if (systemFolderLength == 0 || systemFolderLength + sizeof(libraryName) > sizeof(libraryPath)) {
    return 2;
  }
  memcpy(libraryPath + systemFolderLength, libraryName, sizeof(libraryName));
  DWORD unusedHandle = 0;
  if (GetFileVersionInfoSizeA(libraryPath, &unusedHandle) == 0) {
    return 4;
  }
  // Wakes GetMessage, so the lifetime is checked while nothing else posts a message.
  if (SetTimer(nullptr, 0, 50, nullptr) == 0) {
    return 5;
  }
  const DWORD startedAt = GetTickCount();
  for (;;) {
    MSG message;
    const BOOL result = GetMessageA(&message, nullptr, 0, 0);
    if (result == 0) {
      return static_cast<int>(message.wParam);
    }
    if (result == -1) {
      return 6;
    }
    DispatchMessageA(&message);
    // A game a failed test leaves behind ends by itself.
    if (GetTickCount() - startedAt > lifetimeMilliseconds) {
      return 3;
    }
  }
}
