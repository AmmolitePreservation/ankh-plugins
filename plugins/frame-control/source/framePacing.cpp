#include "framePacing.h"

#include "direct3D9.h"
#include "fatalError.h"
#include "gameForeground.h"
#include "methodTable.h"
#include "sessionLog.h"

static CreateDeviceFunction originalCreateDevice = nullptr;
static PresentFunction originalPresent = nullptr;
static SwapChainPresentFunction originalSwapChainPresent = nullptr;

static unsigned long foregroundFramesPerSecond = 0;
static unsigned long backgroundFramesPerSecond = 0;
static HANDLE timer = nullptr;
static LONGLONG countsPerSecond = 0;
static LONGLONG previousPresent = 0;
static unsigned long previousFramesPerSecond = 0;
static thread_local unsigned presentDepth = 0;

static LONGLONG counterNow() {
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  return counter.QuadPart;
}

// A frame is due one interval after the one before it. The interval starts over when the rate changes, so a
// game coming back to the foreground is not held to its background rate for one more frame.
static void paceFrame() {
  const unsigned long framesPerSecond = isGameInForeground() ? foregroundFramesPerSecond : backgroundFramesPerSecond;
  if (framesPerSecond == 0) {
    previousFramesPerSecond = 0;
    return;
  }
  LONGLONG presentedAt = counterNow();
  if (framesPerSecond == previousFramesPerSecond) {
    const LONGLONG remaining = previousPresent + countsPerSecond / framesPerSecond - presentedAt;
    if (remaining > 0) {
      LARGE_INTEGER due = {};
      due.QuadPart = -((remaining * 10000000 + countsPerSecond - 1) / countsPerSecond);
      if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
        fatalError("Frame pacing timer failed (%lu)", GetLastError());
      }
      presentedAt = counterNow();
    }
  }
  previousPresent = presentedAt;
  previousFramesPerSecond = framesPerSecond;
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

void installFramePacing(const Configuration& configuration) {
  foregroundFramesPerSecond = configuration.foregroundFramesPerSecond;
  backgroundFramesPerSecond = configuration.backgroundFramesPerSecond;
  if (foregroundFramesPerSecond == 0 && backgroundFramesPerSecond == 0) {
    writeLogLine("INFO", "framePacingOff");
    return;
  }
  LARGE_INTEGER frequency = {};
  if (!QueryPerformanceFrequency(&frequency)) {
    fatalError("QueryPerformanceFrequency failed (%lu)", GetLastError());
  }
  countsPerSecond = frequency.QuadPart;
  timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (timer == nullptr) {
    fatalError("A high-resolution timer could not be created (%lu)", GetLastError());
  }
  whenDirect3D9IsCreated(&onDirect3DCreated);
  writeLogLine("INFO", "framePacingEnabled foregroundFPS=%lu backgroundFPS=%lu", foregroundFramesPerSecond,
    backgroundFramesPerSecond);
}
