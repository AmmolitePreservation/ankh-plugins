#include "direct3DMode.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>

// A stand-in game for the tests. It imports the system DLL the loader stands in for, so the loader beside it
// starts with it, and pumps messages on its main thread until a plugin posts WM_QUIT. Started with the arguments
// "direct3D ..." it first acts as a Direct3D 9 game (direct3DMode.h).
// Exit codes: the WM_QUIT's (0 from the test plugin); 2 the call the loader forwards could not be set up; 3 nothing
// ended it within its lifetime; 4 the call the loader forwards failed; 5 the timer did not start; 6 GetMessage failed;
// 7 the arguments name no mode; 8 the Direct3D mode's report could not be opened; 9 a step of the Direct3D mode
// failed, which the report's last line names.

static const DWORD lifetimeMilliseconds = 120000;

#if defined(fixtureImportsVersion)

static int callForwardedExport() {
  static const char libraryName[] = "\\kernel32.dll";
  char libraryPath[MAX_PATH];
  const UINT systemFolderLength = GetSystemDirectoryA(libraryPath, sizeof(libraryPath));
  if (systemFolderLength == 0 || systemFolderLength + sizeof(libraryName) > sizeof(libraryPath)) {
    return 2;
  }
  memcpy(libraryPath + systemFolderLength, libraryName, sizeof(libraryName));
  DWORD unusedHandle = 0;
  return GetFileVersionInfoSizeA(libraryPath, &unusedHandle) != 0 ? 0 : 4;
}

#elif defined(fixtureImportsOpenGL)

// Exported by opengl32.dll and declared in no Windows header.
extern "C" __declspec(dllimport) int WINAPI wglDescribePixelFormat(HDC deviceContext, int pixelFormat,
  UINT descriptorSize, PIXELFORMATDESCRIPTOR* descriptor);

static int callForwardedExport() {
  const HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    return 2;
  }
  PIXELFORMATDESCRIPTOR descriptor = {};
  const int pixelFormatCount = wglDescribePixelFormat(screen, 1, sizeof(descriptor), &descriptor);
  ReleaseDC(nullptr, screen);
  return pixelFormatCount > 0 ? 0 : 4;
}

#else
#error "The build defines which system DLL the fixture game imports (fixture.cmake)"
#endif

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  const int forwardedCallFailure = callForwardedExport();
  if (forwardedCallFailure != 0) {
    return forwardedCallFailure;
  }
  if (__argc > 1) {
    if (strcmp(__argv[1], "direct3D") != 0) {
      return 7;
    }
    const int modeFailure = runDirect3DMode(instance, __argc - 2, __argv + 2);
    if (modeFailure != 0) {
      return modeFailure;
    }
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
