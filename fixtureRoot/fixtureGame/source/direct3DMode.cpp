#include "direct3DMode.h"

#include <initguid.h>
#include <d3d9.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

struct Request {
  bool isFullscreen;
  UINT width;
  UINT height;
};

static const char* const reportFileName = "fixtureReport.txt";
static const long initialClientWidth = 640;
static const long initialClientHeight = 480;
static const unsigned long largestSize = 16384;
static const int presentCount = 10;

static FILE* report = nullptr;
static HWND gameWindow = nullptr;
static IDirect3DDevice9* device = nullptr;

static void writeReport(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  vfprintf(report, format, arguments);
  va_end(arguments);
  fputc('\n', report);
  fflush(report);
}

static bool failStep(const char* step, unsigned long error) {
  writeReport("failed step=%s error=0x%08lX", step, error);
  return false;
}

static bool openReport() {
  std::string path(MAX_PATH, '\0');
  const DWORD length = GetModuleFileNameA(nullptr, path.data(), MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return false;
  }
  path.resize(length);
  path.resize(path.rfind('\\') + 1);
  path += reportFileName;
  report = fopen(path.c_str(), "w");
  return report != nullptr;
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
  if (message == WM_SIZE) {
    writeReport("windowSize kind=%u width=%u height=%u", static_cast<unsigned>(wordParameter), LOWORD(longParameter),
      HIWORD(longParameter));
  } else if (message == WM_MOUSEMOVE) {
    writeReport("mouseMove x=%d y=%d", static_cast<short>(LOWORD(longParameter)), static_cast<short>(HIWORD(longParameter)));
  } else if (message == WM_DESTROY) {
    PostQuitMessage(0);
  }
  return DefWindowProcA(window, message, wordParameter, longParameter);
}

// A framed window the game sizes itself and keeps hidden until its device exists, as windowed games do.
static bool createGameWindow(HINSTANCE instance) {
  WNDCLASSA windowClass = {};
  windowClass.lpfnWndProc = &windowProcedure;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = "fixtureGameWindow";
  if (RegisterClassA(&windowClass) == 0) {
    return failStep("registerClass", GetLastError());
  }
  RECT frame = { 0, 0, initialClientWidth, initialClientHeight };
  if (!AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE)) {
    return failStep("adjustWindowRect", GetLastError());
  }
  gameWindow = CreateWindowExA(0, windowClass.lpszClassName, "fixtureGame", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
    frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, nullptr);
  if (gameWindow == nullptr) {
    return failStep("createWindow", GetLastError());
  }
  writeReport("windowCreated style=0x%08lX", static_cast<unsigned long>(GetWindowLongA(gameWindow, GWL_STYLE)));
  return true;
}

static bool parseSize(const char* text, UINT* size) {
  char* end = nullptr;
  const unsigned long value = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || value == 0 || value > largestSize) {
    return false;
  }
  *size = static_cast<UINT>(value);
  return true;
}

static bool parseRequest(int argumentCount, char** arguments, Request* request) {
  if (argumentCount == 1 && strcmp(arguments[0], "fullscreen") == 0) {
    request->isFullscreen = true;
    return true;
  }
  request->isFullscreen = false;
  return argumentCount == 3 && strcmp(arguments[0], "windowed") == 0 && parseSize(arguments[1], &request->width)
    && parseSize(arguments[2], &request->height);
}

// A fullscreen game sets the display's mode before it creates its device. The fixture asks for the mode the
// display is already in, so the call changes nothing on a machine where no plugin holds it back.
static bool requestDisplayMode(Request* request) {
  DEVMODEA mode = {};
  mode.dmSize = sizeof(mode);
  if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
    return failStep("enumDisplaySettings", GetLastError());
  }
  request->width = mode.dmPelsWidth;
  request->height = mode.dmPelsHeight;
  const LONG result = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN);
  writeReport("displayModeChange result=%ld width=%lu height=%lu", result, mode.dmPelsWidth, mode.dmPelsHeight);
  return true;
}

#if defined(fixtureLoadsLibrariesAtRunTime)

typedef IDirect3D9* (WINAPI* Direct3DCreate9Function)(UINT sdkVersion);
typedef HRESULT (WINAPI* DirectInput8CreateFunction)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);

static FARPROC loadedExport(const char* libraryName, const char* exportName) {
  const HMODULE library = LoadLibraryA(libraryName);
  return library != nullptr ? GetProcAddress(library, exportName) : nullptr;
}

static IDirect3D9* createDirect3D() {
  const Direct3DCreate9Function create = reinterpret_cast<Direct3DCreate9Function>(loadedExport("d3d9.dll", "Direct3DCreate9"));
  return create != nullptr ? create(D3D_SDK_VERSION) : nullptr;
}

static HRESULT createDirectInput(HINSTANCE instance, IDirectInput8A** directInput) {
  const DirectInput8CreateFunction create =
    reinterpret_cast<DirectInput8CreateFunction>(loadedExport("dinput8.dll", "DirectInput8Create"));
  if (create == nullptr) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  return create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void**>(directInput), nullptr);
}

#else

static IDirect3D9* createDirect3D() {
  return Direct3DCreate9(D3D_SDK_VERSION);
}

static HRESULT createDirectInput(HINSTANCE instance, IDirectInput8A** directInput) {
  return DirectInput8Create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void**>(directInput), nullptr);
}

#endif

static D3DPRESENT_PARAMETERS presentParameters(const Request& request) {
  D3DPRESENT_PARAMETERS parameters = {};
  parameters.BackBufferWidth = request.width;
  parameters.BackBufferHeight = request.height;
  parameters.BackBufferFormat = request.isFullscreen ? D3DFMT_X8R8G8B8 : D3DFMT_UNKNOWN;
  parameters.BackBufferCount = 1;
  parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
  parameters.hDeviceWindow = gameWindow;
  parameters.Windowed = request.isFullscreen ? FALSE : TRUE;
  // Nothing but a plugin's pacing holds a frame back, so the time a batch of presents takes is that pacing.
  parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  return parameters;
}

static void reportParameters(const char* event, HRESULT result, const D3DPRESENT_PARAMETERS& parameters) {
  writeReport("%s result=0x%08lX backBufferWidth=%u backBufferHeight=%u windowed=%d swapEffect=%d", event,
    static_cast<unsigned long>(result), parameters.BackBufferWidth, parameters.BackBufferHeight, parameters.Windowed,
    parameters.SwapEffect);
}

static bool reportBackBuffer() {
  IDirect3DSurface9* backBuffer = nullptr;
  HRESULT result = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
  if (FAILED(result)) {
    return failStep("getBackBuffer", static_cast<unsigned long>(result));
  }
  D3DSURFACE_DESC description = {};
  result = backBuffer->GetDesc(&description);
  backBuffer->Release();
  if (FAILED(result)) {
    return failStep("describeBackBuffer", static_cast<unsigned long>(result));
  }
  writeReport("backBuffer width=%u height=%u", description.Width, description.Height);
  return true;
}

static bool createDevice(const Request& request) {
  IDirect3D9* const direct3D = createDirect3D();
  if (direct3D == nullptr) {
    return failStep("createDirect3D", GetLastError());
  }
  D3DPRESENT_PARAMETERS parameters = presentParameters(request);
  // A null reference device would need no graphics hardware, but it has no swap chain or back buffer to show.
  const HRESULT result = direct3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, gameWindow,
    D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device);
  direct3D->Release();
  reportParameters("deviceCreated", result, parameters);
  return SUCCEEDED(result) ? reportBackBuffer() : failStep("createDevice", static_cast<unsigned long>(result));
}

static bool reportPoint(long x, long y) {
  POINT screen = { x, y };
  if (!ClientToScreen(gameWindow, &screen)) {
    return failStep("clientToScreen", GetLastError());
  }
  POINT back = screen;
  if (!ScreenToClient(gameWindow, &back)) {
    return failStep("screenToClient", GetLastError());
  }
  writeReport("pointConversion x=%ld y=%ld screenX=%ld screenY=%ld backX=%ld backY=%ld", x, y, screen.x, screen.y, back.x,
    back.y);
  return true;
}

static bool reportWindowCalls() {
  RECT client = {};
  if (!GetClientRect(gameWindow, &client)) {
    return failStep("getClientRect", GetLastError());
  }
  writeReport("clientRectangle width=%ld height=%ld", client.right, client.bottom);
  if (!reportPoint(0, 0) || !reportPoint(client.right - 1, client.bottom - 1)) {
    return false;
  }
  writeReport("foregroundRequest result=%d", SetForegroundWindow(gameWindow));
  return true;
}

// Each call is undone only when it took effect, so a call a plugin held back is not answered with a second one.
static bool reportCursorCalls() {
  POINT positionBefore = {};
  if (!GetCursorPos(&positionBefore)) {
    return failStep("getCursorPos", GetLastError());
  }
  const BOOL moveResult = SetCursorPos(positionBefore.x + 1, positionBefore.y);
  POINT positionAfter = {};
  if (!GetCursorPos(&positionAfter)) {
    return failStep("getCursorPos", GetLastError());
  }
  const bool isMoved = positionAfter.x != positionBefore.x || positionAfter.y != positionBefore.y;
  writeReport("cursorMove result=%d moved=%d", moveResult, isMoved);
  if (isMoved && !SetCursorPos(positionBefore.x, positionBefore.y)) {
    return failStep("restoreCursorPosition", GetLastError());
  }

  RECT clipBefore = {};
  if (!GetClipCursor(&clipBefore)) {
    return failStep("getClipCursor", GetLastError());
  }
  RECT requestedClip = clipBefore;
  requestedClip.right -= 1;
  const BOOL clipResult = ClipCursor(&requestedClip);
  RECT clipAfter = {};
  if (!GetClipCursor(&clipAfter)) {
    return failStep("getClipCursor", GetLastError());
  }
  const bool isClipChanged = !EqualRect(&clipBefore, &clipAfter);
  writeReport("cursorClip result=%d changed=%d", clipResult, isClipChanged);
  if (isClipChanged && !ClipCursor(&clipBefore)) {
    return failStep("restoreCursorClip", GetLastError());
  }
  return true;
}

// The level a fullscreen game asks for. The keyboard is never acquired, so the request takes no key from the
// desktop on a machine where no plugin changes it.
static bool reportKeyboard(HINSTANCE instance) {
  IDirectInput8A* directInput = nullptr;
  HRESULT result = createDirectInput(instance, &directInput);
  if (FAILED(result)) {
    return failStep("createDirectInput", static_cast<unsigned long>(result));
  }
  IDirectInputDevice8A* keyboard = nullptr;
  result = directInput->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr);
  if (FAILED(result)) {
    return failStep("createKeyboard", static_cast<unsigned long>(result));
  }
  result = keyboard->SetCooperativeLevel(gameWindow, DISCL_EXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY);
  writeReport("cooperativeLevel result=0x%08lX", static_cast<unsigned long>(result));
  keyboard->Release();
  directInput->Release();
  return true;
}

static LONGLONG counterNow() {
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  return counter.QuadPart;
}

static unsigned long millisecondsSince(LONGLONG start) {
  LARGE_INTEGER frequency = {};
  QueryPerformanceFrequency(&frequency);
  return static_cast<unsigned long>((counterNow() - start) * 1000 / frequency.QuadPart);
}

// A game presents through its device or through the device's swap chain; each way gets its own timed batch.
static bool reportPresents() {
  LONGLONG start = counterNow();
  for (int frame = 0; frame < presentCount; frame++) {
    const HRESULT result = device->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(result)) {
      return failStep("devicePresent", static_cast<unsigned long>(result));
    }
  }
  writeReport("devicePresents count=%d milliseconds=%lu", presentCount, millisecondsSince(start));

  IDirect3DSwapChain9* swapChain = nullptr;
  HRESULT result = device->GetSwapChain(0, &swapChain);
  if (FAILED(result)) {
    return failStep("getSwapChain", static_cast<unsigned long>(result));
  }
  start = counterNow();
  for (int frame = 0; frame < presentCount; frame++) {
    result = swapChain->Present(nullptr, nullptr, nullptr, nullptr, 0);
    if (FAILED(result)) {
      return failStep("swapChainPresent", static_cast<unsigned long>(result));
    }
  }
  writeReport("swapChainPresents count=%d milliseconds=%lu", presentCount, millisecondsSince(start));
  swapChain->Release();
  return true;
}

static bool resetDevice(const Request& request) {
  D3DPRESENT_PARAMETERS parameters = presentParameters(request);
  const HRESULT result = device->Reset(&parameters);
  reportParameters("deviceReset", result, parameters);
  return SUCCEEDED(result) ? reportBackBuffer() : failStep("resetDevice", static_cast<unsigned long>(result));
}

// After a device change a windowed game puts its frame back and sizes its window around the back buffer, hidden
// while it does so. The report reads the window back through calls no plugin changes.
static bool reportWindowPlacement(const Request& request) {
  RECT frame = { 0, 0, static_cast<LONG>(request.width), static_cast<LONG>(request.height) };
  if (!AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE)) {
    return failStep("adjustWindowRect", GetLastError());
  }
  SetWindowLongA(gameWindow, GWL_STYLE, GetWindowLongA(gameWindow, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
  const BOOL moveResult = SetWindowPos(gameWindow, nullptr, 40, 40, frame.right - frame.left, frame.bottom - frame.top,
    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
  const bool isHidden = !IsWindowVisible(gameWindow);
  const BOOL showResult = SetWindowPos(gameWindow, nullptr, 0, 0, 0, 0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  RECT placed = {};
  if (!GetWindowRect(gameWindow, &placed)) {
    return failStep("getWindowRect", GetLastError());
  }
  writeReport("windowPlacement moveResult=%d hidden=%d showResult=%d shown=%d style=0x%08lX left=%ld top=%ld width=%ld height=%ld",
    moveResult, isHidden, showResult, IsWindowVisible(gameWindow) != FALSE,
    static_cast<unsigned long>(GetWindowLongA(gameWindow, GWL_STYLE)), placed.left, placed.top, placed.right - placed.left,
    placed.bottom - placed.top);
  return true;
}

int runDirect3DMode(HINSTANCE instance, int argumentCount, char** arguments) {
  Request request = {};
  if (!parseRequest(argumentCount, arguments, &request)) {
    return 7;
  }
  if (!openReport()) {
    return 8;
  }
  if (!createGameWindow(instance) || (request.isFullscreen && !requestDisplayMode(&request)) || !createDevice(request)) {
    return 9;
  }
  ShowWindow(gameWindow, SW_SHOWNOACTIVATE);
  if (!reportWindowCalls() || !reportCursorCalls() || !reportKeyboard(instance) || !reportPresents() || !resetDevice(request)
      || (!request.isFullscreen && !reportWindowPlacement(request))) {
    return 9;
  }
  writeReport("ready");
  return 0;
}
