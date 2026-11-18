#include "framePacing.h"

#include "direct3D8.h"
#include "direct3D9.h"
#include "framePacer.h"
#include "hookRegistry.h"
#include "methodTable.h"
#include "sessionLog.h"

static CreateDeviceFunction originalCreateDevice = nullptr;
static PresentFunction originalPresent = nullptr;
static SwapChainPresentFunction originalSwapChainPresent = nullptr;

static Direct3D8CreateDeviceFunction originalCreateDevice8 = nullptr;
static Device8PresentFunction originalPresent8 = nullptr;

typedef BOOL (WINAPI* SwapBuffersFunction)(HDC);
static SwapBuffersFunction originalSwapBuffers = nullptr;

static thread_local unsigned presentDepth = 0;

// An OpenGL game presents with SwapBuffers.
static BOOL WINAPI swapBuffersDetour(HDC deviceContext) {
  paceFrame();
  return originalSwapBuffers(deviceContext);
}

static HRESULT STDMETHODCALLTYPE presentDetour(IDirect3DDevice9* device, const RECT* source, const RECT* destination,
    HWND overrideWindow, const RGNDATA* dirtyRegion) {
  if (presentDepth == 0) {
    paceFrame();
  }
  presentDepth++;
  const HRESULT result = originalPresent(device, source, destination, overrideWindow, dirtyRegion);
  presentDepth--;
  return result;
}

static HRESULT STDMETHODCALLTYPE swapChainPresentDetour(IDirect3DSwapChain9* swapChain, const RECT* source,
    const RECT* destination, HWND overrideWindow, const RGNDATA* dirtyRegion, DWORD flags) {
  if (presentDepth == 0) {
    paceFrame();
  }
  presentDepth++;
  const HRESULT result = originalSwapChainPresent(swapChain, source, destination, overrideWindow, dirtyRegion, flags);
  presentDepth--;
  return result;
}

static HRESULT STDMETHODCALLTYPE createDeviceDetour(IDirect3D9* direct3D, UINT adapter, D3DDEVTYPE deviceType, HWND focus,
    DWORD behaviorFlags, D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device) {
  const HRESULT result = originalCreateDevice(direct3D, adapter, deviceType, focus, behaviorFlags, parameters, device);
  if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
    hookPresents(*device, &presentDetour, &originalPresent, &swapChainPresentDetour, &originalSwapChainPresent);
  }
  return result;
}

static void onDirect3DCreated(IDirect3D9* direct3D) {
  hookMethod("IDirect3D9::CreateDevice", direct3D, direct3DCreateDeviceSlot, &createDeviceDetour, &originalCreateDevice);
}

static HRESULT STDMETHODCALLTYPE present8Detour(void* device, const RECT* source, const RECT* destination, HWND overrideWindow,
    const void* dirtyRegion) {
  paceFrame();
  return originalPresent8(device, source, destination, overrideWindow, dirtyRegion);
}

static HRESULT STDMETHODCALLTYPE createDevice8Detour(void* direct3D, UINT adapter, DWORD deviceType, HWND focus,
    DWORD behaviorFlags, void* parameters, void** device) {
  const HRESULT result = originalCreateDevice8(direct3D, adapter, deviceType, focus, behaviorFlags, parameters, device);
  if (SUCCEEDED(result) && device != nullptr && *device != nullptr) {
    hookMethod("IDirect3DDevice8::Present", *device, device8PresentSlot, &present8Detour, &originalPresent8);
  }
  return result;
}

static void onDirect3D8Created(void* direct3D) {
  hookMethod("IDirect3D8::CreateDevice", direct3D, direct3D8CreateDeviceSlot, &createDevice8Detour, &originalCreateDevice8);
}

void installFramePacing(const Configuration& configuration) {
  if (configuration.foregroundFramesPerSecond == 0 && configuration.backgroundFramesPerSecond == 0) {
    writeLogLine("INFO", "framePacingOff");
    return;
  }
  startFramePacer(configuration.foregroundFramesPerSecond, configuration.backgroundFramesPerSecond);
  whenDirect3D9IsCreated(&onDirect3DCreated);
  whenDirect3D8IsCreated(&onDirect3D8Created);
  installImportHook(GetModuleHandleA(nullptr), "gdi32.dll", "SwapBuffers", reinterpret_cast<void*>(&swapBuffersDetour),
    reinterpret_cast<void**>(&originalSwapBuffers));
  writeLogLine("INFO", "framePacingEnabled foregroundFPS=%lu backgroundFPS=%lu", configuration.foregroundFramesPerSecond,
    configuration.backgroundFramesPerSecond);
}
