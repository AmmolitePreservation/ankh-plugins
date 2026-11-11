#include "inputHooks.h"

#include "gameForeground.h"
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

// The clip is left as it is rather than released: it belongs to whichever application is active.
static BOOL WINAPI clipCursorDetour(const RECT* rectangle) {
  if (rectangle == nullptr || isGameInForeground()) {
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
  if (!configuration.releaseInput) {
    writeLogLine("INFO", "inputLeftToTheGame");
    return;
  }
  whenModuleLoads("dinput8.dll", &hookDirectInput8Create);
  hookUser32("SetCursorPos", &setCursorPosDetour, &originalSetCursorPos);
  hookUser32("ClipCursor", &clipCursorDetour, &originalClipCursor);
  hookUser32("GetAsyncKeyState", &getAsyncKeyStateDetour, &originalGetAsyncKeyState);
  writeLogLine("INFO", "inputReleased");
}
