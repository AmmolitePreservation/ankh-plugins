#include "direct3D8.h"

#include "fatalError.h"
#include "hookRegistry.h"
#include "moduleWatch.h"

typedef void* (WINAPI* Direct3DCreate8Function)(UINT sdkVersion);

static Direct3DCreate8Function originalDirect3DCreate8 = nullptr;
static void (*createdCallback)(void* direct3D) = nullptr;
static SRWLOCK callbackLock = SRWLOCK_INIT;
static bool hasCalledBack = false;

static void* WINAPI direct3DCreate8Detour(UINT sdkVersion) {
  void* const direct3D = originalDirect3DCreate8(sdkVersion);
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

static void hookDirect3DCreate8() {
  installExportHook("d3d8.dll", "Direct3DCreate8", reinterpret_cast<void*>(&direct3DCreate8Detour),
    reinterpret_cast<void**>(&originalDirect3DCreate8));
}

void whenDirect3D8IsCreated(void (*callback)(void* direct3D)) {
  if (createdCallback != nullptr) {
    fatalError("whenDirect3D8IsCreated called twice");
  }
  createdCallback = callback;
  whenModuleLoads("d3d8.dll", &hookDirect3DCreate8);
}
