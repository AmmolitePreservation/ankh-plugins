#include "gammaHooks.h"

#include "hookRegistry.h"
#include "sessionLog.h"

#include <windows.h>

#include <atomic>

typedef BOOL (WINAPI* SetDeviceGammaRampFunction)(HDC, LPVOID);

static SetDeviceGammaRampFunction originalSetDeviceGammaRamp = nullptr;
static std::atomic<bool> isGammaChangeLogged{false};

// A borderless game shares its monitor with the desktop. A gamma ramp belongs to the whole monitor, stays after the
// game crashes, and on an HDR display bends every color, so the game's brightness setting changes nothing.
static BOOL WINAPI setDeviceGammaRampDetour(HDC context, LPVOID ramp) {
  (void)context;
  (void)ramp;
  if (!isGammaChangeLogged.exchange(true)) {
    writeLogLine("INFO", "gammaChangeIgnored");
  }
  return TRUE;
}

void installGammaHooks() {
  installExportHook("gdi32.dll", "SetDeviceGammaRamp", reinterpret_cast<void*>(&setDeviceGammaRampDetour),
    reinterpret_cast<void**>(&originalSetDeviceGammaRamp));
}
