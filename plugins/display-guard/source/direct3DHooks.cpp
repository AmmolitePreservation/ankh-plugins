#include "direct3DHooks.h"

#include "direct3D9.h"
#include "gameWindow.h"
#include "methodTable.h"
#include "presentationPolicy.h"
#include "sessionLog.h"
#include "windowPlacement.h"

#include <string>

typedef HRESULT (STDMETHODCALLTYPE* CreateSwapChainFunction)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**);
typedef HRESULT (STDMETHODCALLTYPE* ResetFunction)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static CreateDeviceFunction originalCreateDevice = nullptr;
static CreateSwapChainFunction originalCreateAdditionalSwapChain = nullptr;
static ResetFunction originalReset = nullptr;
static PresentFunction originalPresent = nullptr;
static SwapChainPresentFunction originalSwapChainPresent = nullptr;

static std::string monitor;
static RenderResolution renderResolution = RenderResolution::native;
static HWND focusWindow = nullptr;
static RECT presentedPicture = {};
static UINT presentedWidth = 0;
static UINT presentedHeight = 0;
static thread_local unsigned presentDepth = 0;

// The game's request, made windowed: the back buffer keeps its size and takes the desktop's format, as a
// windowed device must, and fullscreen-only settings are cleared. The copy swap effect is the one Present
// accepts a destination rectangle with, which the letterbox needs.
static D3DPRESENT_PARAMETERS windowedParameters(const D3DPRESENT_PARAMETERS& requested) {
  D3DPRESENT_PARAMETERS applied = requested;
  applied.Windowed = TRUE;
  applied.BackBufferFormat = D3DFMT_UNKNOWN;
  applied.BackBufferCount = 1;
  applied.SwapEffect = D3DSWAPEFFECT_COPY;
  applied.MultiSampleType = D3DMULTISAMPLE_NONE;
  applied.MultiSampleQuality = 0;
  applied.FullScreen_RefreshRateInHz = 0;
  const RECT target = monitorRectangle(monitor);
  if (renderResolution == RenderResolution::native || applied.BackBufferWidth == 0) {
    applied.BackBufferWidth = static_cast<UINT>(target.right - target.left);
  }
  if (renderResolution == RenderResolution::native || applied.BackBufferHeight == 0) {
    applied.BackBufferHeight = static_cast<UINT>(target.bottom - target.top);
  }
  return applied;
}

static void logParameters(const char* event, const D3DPRESENT_PARAMETERS& requested, const D3DPRESENT_PARAMETERS& applied,
    HRESULT result) {
  writeLogLine(SUCCEEDED(result) ? "INFO" : "ERROR",
    "%s result=0x%08lX width=%u height=%u requestedWindowed=%d requestedFormat=%d requestedSwapEffect=%d appliedSwapEffect=%d "
    "requestedBackBufferCount=%u requestedMultiSample=%d appliedWidth=%u appliedHeight=%u refreshRate=%u "
    "presentationInterval=0x%08X",
    event, static_cast<unsigned long>(result), requested.BackBufferWidth, requested.BackBufferHeight, requested.Windowed,
    requested.BackBufferFormat, requested.SwapEffect, applied.SwapEffect, requested.BackBufferCount, requested.MultiSampleType,
    applied.BackBufferWidth, applied.BackBufferHeight, requested.FullScreen_RefreshRateInHz, requested.PresentationInterval);
}

static HWND presentWindow(const D3DPRESENT_PARAMETERS& parameters) {
  return parameters.hDeviceWindow != nullptr ? parameters.hDeviceWindow : focusWindow;
}

// The game's own destination is its idea of its window; the window is the monitor now, so every frame
// is letterboxed into it instead. Null when the frame should go where the game asked.
static const RECT* letterboxedDestination(HWND overrideWindow, RECT* picture) {
  HWND window = nullptr;
  UINT width = 0;
  UINT height = 0;
  if (!gameBackBuffer(&window, &width, &height) || (overrideWindow != nullptr && overrideWindow != window) || IsIconic(window)) {
    return nullptr;
  }
  // Window placement is posted by the window procedure. Resizing here can
  // reenter the game and reset D3D while its Present is still on the stack.
  RECT client = {};
  if (!GetClientRect(window, &client) || IsRectEmpty(&client)) {
    return nullptr;
  }
  *picture = presentationRectangle(width, height, client);
  // Bar erasure belongs to normal window repaint handling, not a GDI GetDC
  // inside the render thread's Present. Only invalidate when geometry changes.
  if (!EqualRect(picture, &presentedPicture) || width != presentedWidth || height != presentedHeight) {
    presentedPicture = *picture;
    presentedWidth = width;
    presentedHeight = height;
    writeLogLine("INFO", "presentationChanged sourceWidth=%u sourceHeight=%u left=%ld top=%ld width=%ld height=%ld", width, height,
      picture->left, picture->top, picture->right - picture->left, picture->bottom - picture->top);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
  }
  return picture;
}

static HRESULT STDMETHODCALLTYPE presentDetour(IDirect3DDevice9* device, const RECT* source, const RECT* destination,
    HWND overrideWindow, const RGNDATA* dirtyRegion) {
  if (presentDepth != 0) {
    return originalPresent(device, source, destination, overrideWindow, dirtyRegion);
  }
  RECT picture = {};
  const RECT* const letterboxed = letterboxedDestination(overrideWindow, &picture);
  presentDepth++;
  const HRESULT result = originalPresent(device, source, letterboxed != nullptr ? letterboxed : destination, overrideWindow,
    dirtyRegion);
  presentDepth--;
  return result;
}

// A game can present through the device's swap chain rather than the device.
static HRESULT STDMETHODCALLTYPE swapChainPresentDetour(IDirect3DSwapChain9* swapChain, const RECT* source,
    const RECT* destination, HWND overrideWindow, const RGNDATA* dirtyRegion, DWORD flags) {
  if (presentDepth != 0) {
    return originalSwapChainPresent(swapChain, source, destination, overrideWindow, dirtyRegion, flags);
  }
  RECT picture = {};
  const RECT* const letterboxed = letterboxedDestination(overrideWindow, &picture);
  presentDepth++;
  const HRESULT result = originalSwapChainPresent(swapChain, source, letterboxed != nullptr ? letterboxed : destination,
    overrideWindow, dirtyRegion, flags);
  presentDepth--;
  return result;
}

static HRESULT STDMETHODCALLTYPE resetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters) {
  if (parameters == nullptr) {
    return originalReset(device, parameters);
  }
  const D3DPRESENT_PARAMETERS requested = *parameters;
  D3DPRESENT_PARAMETERS applied = windowedParameters(requested);
  const HRESULT result = originalReset(device, &applied);
  logParameters("deviceReset", requested, applied, result);
  if (SUCCEEDED(result)) {
    *parameters = applied;
    setGameBackBuffer(presentWindow(applied), applied.BackBufferWidth, applied.BackBufferHeight);
  }
  return result;
}

static HRESULT STDMETHODCALLTYPE createAdditionalSwapChainDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters,
    IDirect3DSwapChain9** swapChain) {
  if (parameters == nullptr || swapChain == nullptr) {
    return originalCreateAdditionalSwapChain(device, parameters, swapChain);
  }
  const D3DPRESENT_PARAMETERS requested = *parameters;
  D3DPRESENT_PARAMETERS applied = windowedParameters(requested);
  const HRESULT result = originalCreateAdditionalSwapChain(device, &applied, swapChain);
  logParameters("additionalSwapChainCreated", requested, applied, result);
  if (SUCCEEDED(result)) {
    *parameters = applied;
    setGameBackBuffer(presentWindow(applied), applied.BackBufferWidth, applied.BackBufferHeight);
  }
  return result;
}

static void hookDevice(IDirect3DDevice9* device) {
  hookMethod("IDirect3DDevice9::CreateAdditionalSwapChain", device, deviceCreateAdditionalSwapChainSlot,
    &createAdditionalSwapChainDetour, &originalCreateAdditionalSwapChain);
  hookMethod("IDirect3DDevice9::Reset", device, deviceResetSlot, &resetDetour, &originalReset);
  hookPresents(device, &presentDetour, &originalPresent, &swapChainPresentDetour, &originalSwapChainPresent);
}

static HRESULT STDMETHODCALLTYPE createDeviceDetour(IDirect3D9* direct3D, UINT adapter, D3DDEVTYPE deviceType, HWND focus,
    DWORD behaviorFlags, D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device) {
  if (parameters == nullptr || device == nullptr) {
    return originalCreateDevice(direct3D, adapter, deviceType, focus, behaviorFlags, parameters, device);
  }
  const D3DPRESENT_PARAMETERS requested = *parameters;
  D3DPRESENT_PARAMETERS applied = windowedParameters(requested);
  const HRESULT result = originalCreateDevice(direct3D, adapter, deviceType, focus, behaviorFlags, &applied, device);
  logParameters("deviceCreated", requested, applied, result);
  if (FAILED(result)) {
    return result;
  }
  writeLogLine("INFO", "deviceAdapter adapter=%u deviceType=%d behaviorFlags=0x%08lX", adapter, deviceType, behaviorFlags);
  *parameters = applied;
  focusWindow = focus;
  adoptGameWindow(presentWindow(applied), applied.BackBufferWidth, applied.BackBufferHeight, requested.Windowed == FALSE);
  hookDevice(*device);
  return result;
}

static void onDirect3DCreated(IDirect3D9* direct3D) {
  hookMethod("IDirect3D9::CreateDevice", direct3D, direct3DCreateDeviceSlot, &createDeviceDetour, &originalCreateDevice);
}

void installDirect3DHooks(const Configuration& configuration) {
  monitor = configuration.monitor;
  renderResolution = configuration.renderResolution;
  whenDirect3D9IsCreated(&onDirect3DCreated);
}
