#include "openGLHooks.h"

#include "fatalError.h"
#include "gameWindow.h"
#include "hookRegistry.h"
#include "presentationPolicy.h"
#include "sessionLog.h"
#include "windowPlacement.h"

#include <windows.h>

#include <string>

typedef BOOL (WINAPI* SetPixelFormatFunction)(HDC, int, const PIXELFORMATDESCRIPTOR*);

static SetPixelFormatFunction originalSetPixelFormat = nullptr;
static std::string monitor;
static RenderResolution renderResolution = RenderResolution::native;

static BOOL WINAPI setPixelFormatDetour(HDC deviceContext, int format, const PIXELFORMATDESCRIPTOR* descriptor) {
  const BOOL result = originalSetPixelFormat(deviceContext, format, descriptor);
  if (!result) {
    return result;
  }
  if (renderResolution != RenderResolution::native) {
    fatalError("The game draws with OpenGL into its window itself, so display-guard.cfg's renderResolution must be native");
  }
  const RECT target = monitorRectangle(monitor);
  const SIZE native = nativeBackBufferSize(target.right - target.left, target.bottom - target.top);
  if (native.cx <= 0 || native.cy <= 0) {
    fatalError("The display handler gives an empty picture (%ldx%ld)", native.cx, native.cy);
  }
  writeLogLine("INFO", "openGLPixelFormatSet format=%d width=%ld height=%ld", format, native.cx, native.cy);
  adoptGameWindow(WindowFromDC(deviceContext), static_cast<UINT>(native.cx), static_cast<UINT>(native.cy), false);
  return result;
}

void installOpenGLHooks(const Configuration& configuration) {
  monitor = configuration.monitor;
  renderResolution = configuration.renderResolution;
  installImportHook(GetModuleHandleA(nullptr), "gdi32.dll", "SetPixelFormat", reinterpret_cast<void*>(&setPixelFormatDetour),
    reinterpret_cast<void**>(&originalSetPixelFormat));
}
