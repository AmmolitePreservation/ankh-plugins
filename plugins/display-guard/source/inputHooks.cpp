#include "inputHooks.h"

#include "fatalError.h"
#include "gameForeground.h"
#include "gameWindow.h"
#include "methodTable.h"
#include "moduleWatch.h"
#include "sessionLog.h"
#include "user32Hook.h"

#include <windows.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

typedef HRESULT (WINAPI* DirectInput8CreateFunction)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
typedef HRESULT (STDMETHODCALLTYPE* CreateInputDeviceFunction)(void*, REFGUID, void**, IUnknown*);
typedef HRESULT (STDMETHODCALLTYPE* SetCooperativeLevelFunction)(void*, HWND, DWORD);
typedef BOOL (WINAPI* SetCursorPosFunction)(int, int);
typedef BOOL (WINAPI* ClipCursorFunction)(const RECT*);
typedef SHORT (WINAPI* GetAsyncKeyStateFunction)(int);

// Slots in the method tables of IDirectInput8 and IDirectInputDevice8, the same in their ANSI and Unicode forms.
static const size_t directInputCreateDeviceSlot = 3;
static const size_t inputDeviceSetCooperativeLevelSlot = 13;

static DirectInput8CreateFunction originalDirectInput8Create = nullptr;
static CreateInputDeviceFunction originalCreateInputDevice = nullptr;
static SetCooperativeLevelFunction originalSetCooperativeLevel = nullptr;
static SetCursorPosFunction originalSetCursorPos = nullptr;
static ClipCursorFunction originalClipCursor = nullptr;
static GetAsyncKeyStateFunction originalGetAsyncKeyState = nullptr;

static bool isInputReleased = false;
static bool isConfinementWanted = false;
static bool isConfining = false;
static RECT confinement = {};
static bool isCursorMoveLogged = false;
static bool isCursorClipLogged = false;

static const char* deviceKind(void* device) {
  DIDEVCAPS capabilities = {};
  capabilities.dwSize = sizeof(capabilities);
  if (FAILED(static_cast<IDirectInputDevice8A*>(device)->GetCapabilities(&capabilities))) {
    return "unknown";
  }
  switch (GET_DIDEVICE_TYPE(capabilities.dwDevType)) {
    case DI8DEVTYPE_KEYBOARD:
      return "keyboard";
    case DI8DEVTYPE_MOUSE:
      return "mouse";
    default:
      return "other";
  }
}

static HRESULT STDMETHODCALLTYPE setCooperativeLevelDetour(void* device, HWND window, DWORD requested) {
  const DWORD applied = (requested & ~static_cast<DWORD>(DISCL_EXCLUSIVE | DISCL_BACKGROUND | DISCL_NOWINKEY))
    | DISCL_NONEXCLUSIVE | DISCL_FOREGROUND;
  const HRESULT result = originalSetCooperativeLevel(device, window, applied);
  writeLogLine(SUCCEEDED(result) ? "INFO" : "ERROR", "cooperativeLevelSet device=%s requested=0x%02lX applied=0x%02lX result=0x%08lX",
    deviceKind(device), requested, applied, static_cast<unsigned long>(result));
  return result;
}

static HRESULT STDMETHODCALLTYPE createInputDeviceDetour(void* directInput, REFGUID deviceID, void** device, IUnknown* outer) {
  const HRESULT result = originalCreateInputDevice(directInput, deviceID, device, outer);
  if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
    hookMethod("IDirectInputDevice8::SetCooperativeLevel", *device, inputDeviceSetCooperativeLevelSlot,
      &setCooperativeLevelDetour, &originalSetCooperativeLevel);
  }
  return result;
}

static HRESULT WINAPI directInput8CreateDetour(HINSTANCE instance, DWORD version, REFIID interfaceID, void** directInput,
    IUnknown* outer) {
  const HRESULT result = originalDirectInput8Create(instance, version, interfaceID, directInput, outer);
  if (SUCCEEDED(result) && directInput != nullptr && *directInput != nullptr) {
    hookMethod("IDirectInput8::CreateDevice", *directInput, directInputCreateDeviceSlot, &createInputDeviceDetour,
      &originalCreateInputDevice);
  }
  return result;
}

static void hookDirectInput8Create() {
  installExportHook("dinput8.dll", "DirectInput8Create", reinterpret_cast<void*>(&directInput8CreateDetour),
    reinterpret_cast<void**>(&originalDirectInput8Create));
}

static BOOL WINAPI setCursorPosDetour(int x, int y) {
  if (isGameInForeground()) {
    return originalSetCursorPos(x, y);
  }
  if (!isCursorMoveLogged) {
    isCursorMoveLogged = true;
    writeLogLine("INFO", "cursorMoveIgnored x=%d y=%d", x, y);
  }
  return TRUE;
}

static BOOL confineToWindow(HWND window) {
  RECT rectangle = {};
  if (!GetWindowRect(window, &rectangle)) {
    fatalError("GetWindowRect failed for the game's window (error %lu)", GetLastError());
  }
  const BOOL result = originalClipCursor(&rectangle);
  if (!result) {
    writeLogLine("WARN", "cursorConfinementRefused error=%lu", GetLastError());
    return result;
  }
  if (!isConfining || !EqualRect(&rectangle, &confinement)) {
    writeLogLine("INFO", "cursorConfined left=%ld top=%ld right=%ld bottom=%ld", rectangle.left, rectangle.top, rectangle.right,
      rectangle.bottom);
  }
  isConfining = true;
  confinement = rectangle;
  return result;
}

// The confinement is let go only while it is still the one display-guard set: the window now active may have set its own.
static void releaseConfinement() {
  isConfining = false;
  RECT current = {};
  if (!GetClipCursor(&current)) {
    fatalError("GetClipCursor failed (error %lu)", GetLastError());
  }
  if (!EqualRect(&current, &confinement)) {
    writeLogLine("INFO", "cursorConfinementLeft reason=changedSince");
    return;
  }
  if (!originalClipCursor(nullptr)) {
    writeLogLine("WARN", "cursorReleaseRefused error=%lu", GetLastError());
    return;
  }
  writeLogLine("INFO", "cursorReleased");
}

void updateCursorConfinement(HWND window, bool isActive) {
  if (!isConfinementWanted) {
    return;
  }
  if (isActive) {
    confineToWindow(window);
  } else if (isConfining) {
    releaseConfinement();
  }
}

// A game's own confinement is kept while it is in front; its release then confines the cursor to its window again. In the
// background its confinement is ignored rather than released: the clip belongs to whichever application is active.
static BOOL WINAPI clipCursorDetour(const RECT* rectangle) {
  const bool isInFront = isGameInForeground();
  HWND window = nullptr;
  UINT width = 0;
  UINT height = 0;
  if (rectangle == nullptr && isInFront && isConfinementWanted && gameBackBuffer(&window, &width, &height)) {
    return confineToWindow(window);
  }
  if (rectangle == nullptr || isInFront || !isInputReleased) {
    return originalClipCursor(rectangle);
  }
  if (!isCursorClipLogged) {
    isCursorClipLogged = true;
    writeLogLine("INFO", "cursorClipIgnored left=%ld top=%ld right=%ld bottom=%ld", rectangle->left, rectangle->top,
      rectangle->right, rectangle->bottom);
  }
  return TRUE;
}

static SHORT WINAPI getAsyncKeyStateDetour(int key) {
  return isGameInForeground() ? originalGetAsyncKeyState(key) : 0;
}

void installInputHooks(const Configuration& configuration) {
  isInputReleased = configuration.releaseInput;
  isConfinementWanted = configuration.confineCursor;
  // display-guard's own confinement calls the real ClipCursor whether or not the game imports it.
  originalClipCursor = reinterpret_cast<ClipCursorFunction>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("user32.dll"),
    "ClipCursor")));
  if (originalClipCursor == nullptr) {
    fatalError("user32.dll has no ClipCursor");
  }
  if (isInputReleased || isConfinementWanted) {
    hookUser32("ClipCursor", &clipCursorDetour, &originalClipCursor);
  }
  if (!isInputReleased) {
    writeLogLine("INFO", "inputLeftToTheGame");
    return;
  }
  whenModuleLoads("dinput8.dll", &hookDirectInput8Create);
  hookUser32("SetCursorPos", &setCursorPosDetour, &originalSetCursorPos);
  hookUser32("GetAsyncKeyState", &getAsyncKeyStateDetour, &originalGetAsyncKeyState);
  writeLogLine("INFO", "inputReleased");
}
