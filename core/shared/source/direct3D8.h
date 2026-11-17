#pragma once

#include <windows.h>

#include <cstddef>

// Direct3D 8's interfaces, named by their method table slots; the Windows SDK no longer ships d3d8.h.
const size_t direct3D8CreateDeviceSlot = 15;
const size_t device8PresentSlot = 15;

typedef HRESULT (STDMETHODCALLTYPE* Direct3D8CreateDeviceFunction)(void* direct3D, UINT adapter, DWORD deviceType,
  HWND focusWindow, DWORD behaviorFlags, void* presentParameters, void** device);
typedef HRESULT (STDMETHODCALLTYPE* Device8PresentFunction)(void* device, const RECT* source, const RECT* destination,
  HWND overrideWindow, const void* dirtyRegion);

// Calls back once with the first IDirect3D8 the game creates, on the thread creating it and before the game has it.
// Every IDirect3D8 shares one method table, so hooks on this object's methods cover the ones created later.
// initializeHookRegistry comes first.
void whenDirect3D8IsCreated(void (*callback)(void* direct3D));
