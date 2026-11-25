#include "windowPlacement.h"

#include "fatalError.h"

#include <vector>

struct MonitorSearch {
  std::string wantedDeviceName;
  bool wantPrimary;
  bool found;
  RECT rectangle;
  std::vector<std::string> deviceNames;
};

static BOOL CALLBACK visitMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
  MonitorSearch* const search = reinterpret_cast<MonitorSearch*>(parameter);
  MONITORINFOEXA information = {};
  information.cbSize = sizeof(information);
  if (!GetMonitorInfoA(monitor, &information)) {
    fatalError("GetMonitorInfoA failed (error %lu)", GetLastError());
  }
  const std::string deviceName = information.szDevice;
  search->deviceNames.push_back(deviceName.substr(deviceName.find_last_of('\\') + 1));
  const bool isPrimary = (information.dwFlags & MONITORINFOF_PRIMARY) != 0;
  const bool isWanted = search->wantPrimary ? isPrimary : _stricmp(deviceName.c_str(), search->wantedDeviceName.c_str()) == 0;
  if (isWanted) {
    search->found = true;
    search->rectangle = information.rcMonitor;
  }
  return TRUE;
}

RECT monitorRectangle(const std::string& monitor) {
  MonitorSearch search = { "\\\\.\\" + monitor, _stricmp(monitor.c_str(), "primary") == 0, false, {}, {} };
  if (!EnumDisplayMonitors(nullptr, nullptr, &visitMonitor, reinterpret_cast<LPARAM>(&search))) {
    fatalError("EnumDisplayMonitors failed (error %lu)", GetLastError());
  }
  if (!search.found) {
    std::string available;
    for (const std::string& name : search.deviceNames) {
      available += ", " + name;
    }
    fatalError("display-guard.cfg names monitor '%s', but the monitors are primary%s", monitor.c_str(), available.c_str());
  }
  return search.rectangle;
}

LONG borderlessStyle(LONG style) {
  return (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER)) | WS_POPUP;
}

LONG borderlessExtendedStyle(LONG extendedStyle) {
  return extendedStyle & ~(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
}

bool placeBorderless(HWND window, const std::string& monitor) {
  const RECT target = monitorRectangle(monitor);
  const LONG style = GetWindowLongA(window, GWL_STYLE);
  const LONG extendedStyle = GetWindowLongA(window, GWL_EXSTYLE);
  const LONG wantedStyle = borderlessStyle(style);
  const LONG wantedExtendedStyle = borderlessExtendedStyle(extendedStyle);
  RECT current = {};
  if (!GetWindowRect(window, &current)) {
    fatalError("GetWindowRect failed for the game's window (error %lu)", GetLastError());
  }
  if (style == wantedStyle && extendedStyle == wantedExtendedStyle && EqualRect(&current, &target)) {
    return false;
  }
  SetWindowLongA(window, GWL_STYLE, wantedStyle);
  SetWindowLongA(window, GWL_EXSTYLE, wantedExtendedStyle);
  if (!SetWindowPos(window, HWND_NOTOPMOST, target.left, target.top, target.right - target.left, target.bottom - target.top,
      SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
    fatalError("SetWindowPos failed for the game's window (error %lu)", GetLastError());
  }
  return true;
}

RECT letterboxRectangle(unsigned long sourceWidth, unsigned long sourceHeight, const RECT& target) {
  const long targetWidth = target.right - target.left;
  const long targetHeight = target.bottom - target.top;
  long width = targetWidth;
  long height = static_cast<long>(static_cast<unsigned long long>(targetWidth) * sourceHeight / sourceWidth);
  if (height > targetHeight) {
    height = targetHeight;
    width = static_cast<long>(static_cast<unsigned long long>(targetHeight) * sourceWidth / sourceHeight);
  }
  const long left = target.left + (targetWidth - width) / 2;
  const long top = target.top + (targetHeight - height) / 2;
  return { left, top, left + width, top + height };
}
