#include "gameWindow.h"

#include "fatalError.h"
#include "gameForeground.h"
#include "presentationPolicy.h"
#include "sessionLog.h"
#include "user32Hook.h"
#include "windowPlacement.h"

#include <windowsx.h>

#include <string>

typedef LONG (WINAPI* SetWindowLongFunction)(HWND, int, LONG);
#ifdef _WIN64
typedef LONG_PTR (WINAPI* SetWindowLongPtrFunction)(HWND, int, LONG_PTR);
#endif
typedef BOOL (WINAPI* SetWindowPosFunction)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL (WINAPI* GetClientRectFunction)(HWND, RECT*);
typedef BOOL (WINAPI* ConvertPointFunction)(HWND, POINT*);
typedef BOOL (WINAPI* SetForegroundWindowFunction)(HWND);
typedef BOOL (WINAPI* ShowWindowFunction)(HWND, int);
typedef LONG (WINAPI* ChangeDisplaySettingsAFunction)(DEVMODEA*, DWORD);
typedef LONG (WINAPI* ChangeDisplaySettingsWFunction)(DEVMODEW*, DWORD);
typedef LONG (WINAPI* ChangeDisplaySettingsExAFunction)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID);
typedef LONG (WINAPI* ChangeDisplaySettingsExWFunction)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);

struct Letterbox {
  RECT picture;
  UINT backBufferWidth;
  UINT backBufferHeight;
};

struct GameWindow {
  HWND window;
  WNDPROC procedure;
  bool isUnicode;
  UINT backBufferWidth;
  UINT backBufferHeight;
};

static SetWindowLongFunction originalSetWindowLongA = nullptr;
static SetWindowLongFunction originalSetWindowLongW = nullptr;
#ifdef _WIN64
static SetWindowLongPtrFunction originalSetWindowLongPtrA = nullptr;
static SetWindowLongPtrFunction originalSetWindowLongPtrW = nullptr;
#endif
static SetWindowPosFunction originalSetWindowPos = nullptr;
static GetClientRectFunction originalGetClientRect = nullptr;
static ConvertPointFunction originalClientToScreen = nullptr;
static ConvertPointFunction originalScreenToClient = nullptr;
static SetForegroundWindowFunction originalSetForegroundWindow = nullptr;
static ShowWindowFunction originalShowWindow = nullptr;
static ChangeDisplaySettingsAFunction originalChangeDisplaySettingsA = nullptr;
static ChangeDisplaySettingsWFunction originalChangeDisplaySettingsW = nullptr;
static ChangeDisplaySettingsExAFunction originalChangeDisplaySettingsExA = nullptr;
static ChangeDisplaySettingsExWFunction originalChangeDisplaySettingsExW = nullptr;

static std::string monitor;
static bool isStartupShowPending = false;
static bool isForegroundRequestLogged = false;
static bool isRestorePending = false;
static UINT restoreMessage = 0;
static SRWLOCK stateLock = SRWLOCK_INIT;
static GameWindow gameWindow = {};

static GameWindow currentGame() {
  AcquireSRWLockShared(&stateLock);
  const GameWindow game = gameWindow;
  ReleaseSRWLockShared(&stateLock);
  return game;
}

static bool isGameWindow(HWND window) {
  return window != nullptr && window == currentGame().window;
}

bool gameBackBuffer(HWND* window, UINT* width, UINT* height) {
  const GameWindow game = currentGame();
  if (game.window == nullptr || game.backBufferWidth == 0) {
    return false;
  }
  *window = game.window;
  *width = game.backBufferWidth;
  *height = game.backBufferHeight;
  return true;
}

static bool letterboxOf(HWND window, Letterbox* letterbox) {
  const GameWindow game = currentGame();
  if (window == nullptr || window != game.window || game.backBufferWidth == 0) {
    return false;
  }
  // The window covers the monitor, so the picture is placed in the monitor's size rather than the live client
  // rectangle, which is empty while the window is minimized.
  const RECT target = monitorRectangle(monitor);
  const RECT client = { 0, 0, target.right - target.left, target.bottom - target.top };
  const RECT picture = presentationRectangle(game.backBufferWidth, game.backBufferHeight, client);
  if (IsRectEmpty(&picture)) {
    fatalError("A %ux%u back buffer has no picture in a %ldx%ld window", game.backBufferWidth, game.backBufferHeight,
      client.right, client.bottom);
  }
  *letterbox = { picture, game.backBufferWidth, game.backBufferHeight };
  return true;
}

static long floorDivide(long long numerator, long long denominator) {
  const long long quotient = numerator / denominator;
  return static_cast<long>(numerator % denominator != 0 && numerator < 0 ? quotient - 1 : quotient);
}

static POINT backBufferPoint(const Letterbox& letterbox, POINT client) {
  const RECT& picture = letterbox.picture;
  return {
    floorDivide(static_cast<long long>(client.x - picture.left) * letterbox.backBufferWidth, picture.right - picture.left),
    floorDivide(static_cast<long long>(client.y - picture.top) * letterbox.backBufferHeight, picture.bottom - picture.top),
  };
}

// The center of the back-buffer pixel, so mapping the result back gives the same pixel.
static POINT clientPoint(const Letterbox& letterbox, POINT backBuffer) {
  const RECT& picture = letterbox.picture;
  return {
    picture.left + floorDivide((2LL * backBuffer.x + 1) * (picture.right - picture.left), 2LL * letterbox.backBufferWidth),
    picture.top + floorDivide((2LL * backBuffer.y + 1) * (picture.bottom - picture.top), 2LL * letterbox.backBufferHeight),
  };
}

static long nearestPixel(long position, UINT pixelCount) {
  if (position < 0) {
    return 0;
  }
  return position >= static_cast<long>(pixelCount) ? static_cast<long>(pixelCount) - 1 : position;
}

// Wheel messages carry screen coordinates; the other mouse messages carry client coordinates.
static bool hasClientPosition(UINT message) {
  return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL;
}

static void eraseBars(HWND window, HDC deviceContext, const RECT& picture) {
  RECT client = {};
  if (!GetClientRect(window, &client)) {
    fatalError("GetClientRect failed for the game's window (error %lu)", GetLastError());
  }
  const RECT bars[] = {
    { client.left, client.top, picture.left, client.bottom },
    { picture.right, client.top, client.right, client.bottom },
    { picture.left, client.top, picture.right, picture.top },
    { picture.left, picture.bottom, picture.right, client.bottom },
  };
  for (const RECT& bar : bars) {
    if (!IsRectEmpty(&bar)) {
      FillRect(deviceContext, &bar, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
  }
}

static LRESULT CALLBACK gameWindowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
  const GameWindow game = currentGame();
  if (message == restoreMessage) {
    isRestorePending = false;
    if (!IsIconic(window)) {
      placeBorderless(window, monitor);
    }
    return 0;
  }
  // Placement is posted rather than done here: resizing inside the game's own size handling can reenter it.
  if (message == WM_SIZE && wordParameter != SIZE_MINIMIZED && !isRestorePending) {
    isRestorePending = PostMessageA(window, restoreMessage, 0, 0) != FALSE;
  }
  Letterbox letterbox = {};
  if (message == WM_ERASEBKGND && wordParameter != 0 && !IsIconic(window) && letterboxOf(window, &letterbox)) {
    eraseBars(window, reinterpret_cast<HDC>(wordParameter), letterbox.picture);
    return 1;
  }
  if (message == WM_SIZE && wordParameter != SIZE_MINIMIZED && game.backBufferWidth != 0) {
    longParameter = MAKELPARAM(game.backBufferWidth, game.backBufferHeight);
  }
  if (hasClientPosition(message) && letterboxOf(window, &letterbox)) {
    // A position over the bars is moved to the nearest picture pixel: the game reads message positions as
    // unsigned, so a point left of or above the picture would wrap to its far edge.
    const POINT mapped = backBufferPoint(letterbox, { GET_X_LPARAM(longParameter), GET_Y_LPARAM(longParameter) });
    longParameter = MAKELPARAM(static_cast<WORD>(nearestPixel(mapped.x, letterbox.backBufferWidth)),
      static_cast<WORD>(nearestPixel(mapped.y, letterbox.backBufferHeight)));
  }
  return game.isUnicode ? CallWindowProcW(game.procedure, window, message, wordParameter, longParameter)
    : CallWindowProcA(game.procedure, window, message, wordParameter, longParameter);
}

enum class Subclassing { done, secondWindow, failed };

// The procedure is replaced under the lock, so the window's thread cannot run the new one before the old one is
// stored. Nothing fatal happens under it: the fatal message box pumps messages into the window procedure.
static Subclassing subclassGameWindow(HWND window, DWORD* error) {
  Subclassing outcome = Subclassing::done;
  AcquireSRWLockExclusive(&stateLock);
  if (gameWindow.window != nullptr && IsWindow(gameWindow.window)) {
    outcome = Subclassing::secondWindow;
  } else {
    const bool isUnicode = IsWindowUnicode(window) != FALSE;
    const LONG_PTR replacement = reinterpret_cast<LONG_PTR>(&gameWindowProcedure);
    const LONG_PTR previous = isUnicode ? SetWindowLongPtrW(window, GWLP_WNDPROC, replacement)
      : SetWindowLongPtrA(window, GWLP_WNDPROC, replacement);
    if (previous == 0) {
      *error = GetLastError();
      outcome = Subclassing::failed;
    } else {
      gameWindow = { window, reinterpret_cast<WNDPROC>(previous), isUnicode, 0, 0 };
      isRestorePending = false;
    }
  }
  ReleaseSRWLockExclusive(&stateLock);
  return outcome;
}

static void logAdoption(HWND window) {
  RECT rectangle = {};
  if (!GetWindowRect(window, &rectangle)) {
    fatalError("GetWindowRect failed for the game's window (error %lu)", GetLastError());
  }
  writeLogLine("INFO",
    "gameWindowAdopted window=%p unicode=%s style=0x%08lX extendedStyle=0x%08lX left=%ld top=%ld width=%ld height=%ld visible=%s",
    static_cast<void*>(window), IsWindowUnicode(window) ? "true" : "false",
    static_cast<unsigned long>(GetWindowLongA(window, GWL_STYLE)), static_cast<unsigned long>(GetWindowLongA(window, GWL_EXSTYLE)),
    rectangle.left, rectangle.top, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
    IsWindowVisible(window) ? "true" : "false");
}

void adoptGameWindow(HWND window, UINT backBufferWidth, UINT backBufferHeight) {
  if (window == nullptr) {
    fatalError("The game created a Direct3D device with no window to present into");
  }
  const bool isNew = !isGameWindow(window);
  if (isNew) {
    if ((GetWindowLongA(window, GWL_STYLE) & WS_CHILD) != 0) {
      fatalError("The game's Direct3D device presents into a child window, which display-guard cannot place");
    }
    DWORD error = 0;
    const Subclassing outcome = subclassGameWindow(window, &error);
    if (outcome == Subclassing::secondWindow) {
      fatalError("The game created a Direct3D device for a second window while its first one still exists");
    }
    if (outcome == Subclassing::failed) {
      fatalError("Replacing the game window's procedure failed (error %lu)", error);
    }
    logAdoption(window);
  }
  setGameBackBuffer(window, backBufferWidth, backBufferHeight);
  if (!isNew) {
    return;
  }
  if (!IsIconic(window)) {
    placeBorderless(window, monitor);
  }
  if (isStartupShowPending && IsWindowVisible(window)) {
    isStartupShowPending = false;
    ShowWindow(window, SW_SHOWMINNOACTIVE);
    writeLogLine("INFO", "startupMinimized");
  }
}

void setGameBackBuffer(HWND window, UINT width, UINT height) {
  if (width == 0 || height == 0) {
    fatalError("The Direct3D device has an empty back buffer");
  }
  AcquireSRWLockExclusive(&stateLock);
  const bool isForGameWindow = window != nullptr && window == gameWindow.window;
  if (isForGameWindow) {
    gameWindow.backBufferWidth = width;
    gameWindow.backBufferHeight = height;
  }
  ReleaseSRWLockExclusive(&stateLock);
  if (!isForGameWindow) {
    fatalError("The Direct3D device presents into a window that is not the game's main window");
  }
  writeLogLine("INFO", "gameWindowMapped backBufferWidth=%u backBufferHeight=%u", width, height);
}

static LONG_PTR permittedWindowLong(HWND window, int index, LONG_PTR value) {
  if ((index != GWL_STYLE && index != GWL_EXSTYLE) || !isGameWindow(window)) {
    return value;
  }
  const LONG requested = static_cast<LONG>(value);
  const LONG applied = index == GWL_STYLE ? borderlessStyle(requested) : borderlessExtendedStyle(requested);
  writeLogLine("INFO", "gameWindowRestyled index=%d requested=0x%08lX applied=0x%08lX", index,
    static_cast<unsigned long>(requested), static_cast<unsigned long>(applied));
  return applied;
}

static LONG WINAPI setWindowLongADetour(HWND window, int index, LONG value) {
  return originalSetWindowLongA(window, index, static_cast<LONG>(permittedWindowLong(window, index, value)));
}

static LONG WINAPI setWindowLongWDetour(HWND window, int index, LONG value) {
  return originalSetWindowLongW(window, index, static_cast<LONG>(permittedWindowLong(window, index, value)));
}

#ifdef _WIN64
static LONG_PTR WINAPI setWindowLongPtrADetour(HWND window, int index, LONG_PTR value) {
  return originalSetWindowLongPtrA(window, index, permittedWindowLong(window, index, value));
}

static LONG_PTR WINAPI setWindowLongPtrWDetour(HWND window, int index, LONG_PTR value) {
  return originalSetWindowLongPtrW(window, index, permittedWindowLong(window, index, value));
}
#endif

// Where the window is and how large is display-guard's; whether it is shown stays the game's.
static BOOL WINAPI setWindowPosDetour(HWND window, HWND insertAfter, int left, int top, int width, int height, UINT flags) {
  if (!isGameWindow(window)) {
    return originalSetWindowPos(window, insertAfter, left, top, width, height, flags);
  }
  writeLogLine("INFO", "gameWindowMoveReplaced insertAfter=%p left=%d top=%d width=%d height=%d flags=0x%04X",
    static_cast<void*>(insertAfter), left, top, width, height, flags);
  if (!IsIconic(window)) {
    placeBorderless(window, monitor);
  }
  const UINT visibility = flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW);
  if (visibility == 0) {
    return TRUE;
  }
  return originalSetWindowPos(window, nullptr, 0, 0, 0, 0,
    visibility | (flags & SWP_NOACTIVATE) | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

static BOOL WINAPI getClientRectDetour(HWND window, RECT* rectangle) {
  const GameWindow game = currentGame();
  if (rectangle == nullptr || window == nullptr || window != game.window || game.backBufferWidth == 0) {
    return originalGetClientRect(window, rectangle);
  }
  *rectangle = { 0, 0, static_cast<LONG>(game.backBufferWidth), static_cast<LONG>(game.backBufferHeight) };
  return TRUE;
}

static BOOL WINAPI clientToScreenDetour(HWND window, POINT* point) {
  Letterbox letterbox = {};
  if (point != nullptr && letterboxOf(window, &letterbox)) {
    *point = clientPoint(letterbox, *point);
  }
  return originalClientToScreen(window, point);
}

static BOOL WINAPI screenToClientDetour(HWND window, POINT* point) {
  const BOOL result = originalScreenToClient(window, point);
  Letterbox letterbox = {};
  if (result && point != nullptr && letterboxOf(window, &letterbox)) {
    *point = backBufferPoint(letterbox, *point);
  }
  return result;
}

// While inactive, a game's fullscreen message loop asks for the foreground on every message. It may still move the
// focus among its own windows.
static BOOL WINAPI setForegroundWindowDetour(HWND window) {
  if (isGameInForeground()) {
    return originalSetForegroundWindow(window);
  }
  if (!isForegroundRequestLogged) {
    isForegroundRequestLogged = true;
    writeLogLine("INFO", "foregroundRequestIgnored");
  }
  return FALSE;
}

static BOOL WINAPI showWindowDetour(HWND window, int command) {
  if (!isStartupShowPending || command == SW_HIDE || !isGameWindow(window)) {
    return originalShowWindow(window, command);
  }
  isStartupShowPending = false;
  writeLogLine("INFO", "startupMinimized");
  return originalShowWindow(window, SW_SHOWMINNOACTIVE);
}

// Mode enumeration and testing stay real. Applying a mode belongs to Windows, not a borderless game: neither startup
// nor Alt+Tab should change the desktop's mode.
static bool isModeChangeIgnored(DWORD flags, DWORD width, DWORD height) {
  if ((flags & CDS_TEST) != 0) {
    return false;
  }
  writeLogLine("INFO", "displayModeChangeIgnored flags=0x%08lX width=%lu height=%lu", flags, width, height);
  return true;
}

static LONG WINAPI changeDisplaySettingsADetour(DEVMODEA* mode, DWORD flags) {
  if (isModeChangeIgnored(flags, mode != nullptr ? mode->dmPelsWidth : 0, mode != nullptr ? mode->dmPelsHeight : 0)) {
    return DISP_CHANGE_SUCCESSFUL;
  }
  return originalChangeDisplaySettingsA(mode, flags);
}

static LONG WINAPI changeDisplaySettingsWDetour(DEVMODEW* mode, DWORD flags) {
  if (isModeChangeIgnored(flags, mode != nullptr ? mode->dmPelsWidth : 0, mode != nullptr ? mode->dmPelsHeight : 0)) {
    return DISP_CHANGE_SUCCESSFUL;
  }
  return originalChangeDisplaySettingsW(mode, flags);
}

static LONG WINAPI changeDisplaySettingsExADetour(LPCSTR device, DEVMODEA* mode, HWND window, DWORD flags, LPVOID parameter) {
  if (isModeChangeIgnored(flags, mode != nullptr ? mode->dmPelsWidth : 0, mode != nullptr ? mode->dmPelsHeight : 0)) {
    return DISP_CHANGE_SUCCESSFUL;
  }
  return originalChangeDisplaySettingsExA(device, mode, window, flags, parameter);
}

static LONG WINAPI changeDisplaySettingsExWDetour(LPCWSTR device, DEVMODEW* mode, HWND window, DWORD flags, LPVOID parameter) {
  if (isModeChangeIgnored(flags, mode != nullptr ? mode->dmPelsWidth : 0, mode != nullptr ? mode->dmPelsHeight : 0)) {
    return DISP_CHANGE_SUCCESSFUL;
  }
  return originalChangeDisplaySettingsExW(device, mode, window, flags, parameter);
}

void installGameWindowHooks(const Configuration& configuration) {
  monitor = configuration.monitor;
  isStartupShowPending = configuration.startMinimized;
  restoreMessage = RegisterWindowMessageA("ankh-plugins.display-guard.restoreBorderless");
  if (restoreMessage == 0) {
    fatalError("RegisterWindowMessageA failed (error %lu)", GetLastError());
  }
  unsigned hookedCount = 0;
  if (configuration.startMinimized) {
    hookedCount += hookUser32("ShowWindow", &showWindowDetour, &originalShowWindow);
  }
  hookedCount += hookUser32("ChangeDisplaySettingsA", &changeDisplaySettingsADetour, &originalChangeDisplaySettingsA);
  hookedCount += hookUser32("ChangeDisplaySettingsW", &changeDisplaySettingsWDetour, &originalChangeDisplaySettingsW);
  hookedCount += hookUser32("ChangeDisplaySettingsExA", &changeDisplaySettingsExADetour, &originalChangeDisplaySettingsExA);
  hookedCount += hookUser32("ChangeDisplaySettingsExW", &changeDisplaySettingsExWDetour, &originalChangeDisplaySettingsExW);
  hookedCount += hookUser32("SetWindowLongA", &setWindowLongADetour, &originalSetWindowLongA);
  hookedCount += hookUser32("SetWindowLongW", &setWindowLongWDetour, &originalSetWindowLongW);
#ifdef _WIN64
  hookedCount += hookUser32("SetWindowLongPtrA", &setWindowLongPtrADetour, &originalSetWindowLongPtrA);
  hookedCount += hookUser32("SetWindowLongPtrW", &setWindowLongPtrWDetour, &originalSetWindowLongPtrW);
#endif
  hookedCount += hookUser32("SetWindowPos", &setWindowPosDetour, &originalSetWindowPos);
  hookedCount += hookUser32("GetClientRect", &getClientRectDetour, &originalGetClientRect);
  hookedCount += hookUser32("ClientToScreen", &clientToScreenDetour, &originalClientToScreen);
  hookedCount += hookUser32("ScreenToClient", &screenToClientDetour, &originalScreenToClient);
  hookedCount += hookUser32("SetForegroundWindow", &setForegroundWindowDetour, &originalSetForegroundWindow);
  if (hookedCount == 0) {
    fatalError("The game's executable imports none of the window functions display-guard changes: its window code "
      "is in a library, which display-guard does not cover");
  }
}
