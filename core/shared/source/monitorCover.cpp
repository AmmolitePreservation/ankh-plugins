#include "monitorCover.h"

#include "fatalError.h"
#include "sessionLog.h"

#include <atomic>

namespace {

const wchar_t* const coverClassName = L"ankhPluginsMonitorCover";
// The lowest opacity Windows still draws: the pixel shows as 1/255 of black over the game.
const BYTE coverAlpha = 1;

std::atomic<bool> isCoverStarted{false};
RECT coveredMonitor = {};
HANDLE coverReady = nullptr;
DWORD coverError = 0;

LRESULT CALLBACK coverProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
  if (message == WM_MOUSEACTIVATE) {
    return MA_NOACTIVATE;
  }
  if (message == WM_NCHITTEST) {
    return HTTRANSPARENT;
  }
  return DefWindowProcW(window, message, wordParameter, longParameter);
}

DWORD WINAPI coverThread(void*) {
  WNDCLASSEXW windowClass = {};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &coverProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = coverClassName;
  windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    coverError = GetLastError();
    SetEvent(coverReady);
    return 1;
  }
  const HWND cover = CreateWindowExW(
    WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, coverClassName, L"", WS_POPUP,
    coveredMonitor.right - 1, coveredMonitor.bottom - 1, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (cover == nullptr || !SetLayeredWindowAttributes(cover, 0, coverAlpha, LWA_ALPHA)) {
    coverError = GetLastError();
    SetEvent(coverReady);
    return 1;
  }
  ShowWindow(cover, SW_SHOWNA);
  writeLogLine("INFO", "monitorCornerCovered window=%p x=%ld y=%ld", static_cast<void*>(cover), coveredMonitor.right - 1,
    coveredMonitor.bottom - 1);
  SetEvent(coverReady);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    DispatchMessageW(&message);
  }
  return 0;
}

}

void coverMonitorCorner(const RECT& monitor) {
  if (isCoverStarted.exchange(true)) {
    return;
  }
  coveredMonitor = monitor;
  coverReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (coverReady == nullptr) {
    fatalError("CreateEvent failed for the monitor cover (error %lu)", GetLastError());
  }
  const HANDLE thread = CreateThread(nullptr, 0, &coverThread, nullptr, 0, nullptr);
  if (thread == nullptr) {
    fatalError("The monitor cover's thread did not start (error %lu)", GetLastError());
  }
  CloseHandle(thread);
  if (WaitForSingleObject(coverReady, INFINITE) != WAIT_OBJECT_0) {
    fatalError("Waiting for the monitor cover failed (error %lu)", GetLastError());
  }
  CloseHandle(coverReady);
  if (coverError != 0) {
    fatalError("The monitor cover could not be made (error %lu)", coverError);
  }
}
