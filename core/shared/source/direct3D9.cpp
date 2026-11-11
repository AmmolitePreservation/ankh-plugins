#include "direct3D9.h"

#include "fatalError.h"
#include "hookRegistry.h"
#include "methodTable.h"
#include "moduleWatch.h"

typedef IDirect3D9* (WINAPI* Direct3DCreate9Function)(UINT sdkVersion);

static Direct3DCreate9Function originalDirect3DCreate9 = nullptr;
static void (*createdCallback)(IDirect3D9* direct3D) = nullptr;
static SRWLOCK callbackLock = SRWLOCK_INIT;
static bool hasCalledBack = false;

static IDirect3D9* WINAPI direct3DCreate9Detour(UINT sdkVersion) {
  IDirect3D9* const direct3D = originalDirect3DCreate9(sdkVersion);
  if (direct3D != nullptr) {
    // Held through the callback: a second thread creating its own object waits until the hooks are in.
    AcquireSRWLockExclusive(&callbackLock);
    if (!hasCalledBack) {
      hasCalledBack = true;
      createdCallback(direct3D);
    }
    ReleaseSRWLockExclusive(&callbackLock);
  }
  return direct3D;
}

static void hookDirect3DCreate9() {
  installExportHook("d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(&direct3DCreate9Detour),
    reinterpret_cast<void**>(&originalDirect3DCreate9));
}

void whenDirect3D9IsCreated(void (*callback)(IDirect3D9* direct3D)) {
  if (createdCallback != nullptr) {
    fatalError("whenDirect3D9IsCreated called twice");
  }
  createdCallback = callback;
  whenModuleLoads("d3d9.dll", &hookDirect3DCreate9);
}

void hookPresents(IDirect3DDevice9* device, PresentFunction presentDetour, PresentFunction* originalPresent,
    SwapChainPresentFunction swapChainPresentDetour, SwapChainPresentFunction* originalSwapChainPresent) {
  hookMethod("IDirect3DDevice9::Present", device, devicePresentSlot, presentDetour, originalPresent);
  IDirect3DSwapChain9* swapChain = nullptr;
  const HRESULT result = device->GetSwapChain(0, &swapChain);
  if (FAILED(result)) {
    fatalError("IDirect3DDevice9::GetSwapChain failed (0x%08lX)", static_cast<unsigned long>(result));
  }
  hookMethod("IDirect3DSwapChain9::Present", swapChain, swapChainPresentSlot, swapChainPresentDetour, originalSwapChainPresent);
  swapChain->Release();
}
