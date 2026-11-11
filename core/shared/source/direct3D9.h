#pragma once

#include <windows.h>
#include <d3d9.h>

#include <cstddef>

// Slots in the method tables of IDirect3D9, IDirect3DDevice9 and IDirect3DSwapChain9.
const size_t direct3DCreateDeviceSlot = 16;
const size_t deviceCreateAdditionalSwapChainSlot = 13;
const size_t deviceResetSlot = 16;
const size_t devicePresentSlot = 17;
const size_t swapChainPresentSlot = 3;

typedef HRESULT (STDMETHODCALLTYPE* CreateDeviceFunction)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
  IDirect3DDevice9**);
typedef HRESULT (STDMETHODCALLTYPE* PresentFunction)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE* SwapChainPresentFunction)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND,
  const RGNDATA*, DWORD);

// Calls back once with the first IDirect3D9 the game creates, on the thread creating it and before the game has
// it. Every IDirect3D9 shares one method table, so hooks on this object's methods cover the ones created later.
// initializeHookRegistry comes first.
void whenDirect3D9IsCreated(void (*callback)(IDirect3D9* direct3D));

// Hooks the two ways a game presents a frame, the device's Present and its implicit swap chain's, once each
// however many devices come. One can call the other, so a detour that must act once a frame counts its depth.
void hookPresents(IDirect3DDevice9* device, PresentFunction presentDetour, PresentFunction* originalPresent,
  SwapChainPresentFunction swapChainPresentDetour, SwapChainPresentFunction* originalSwapChainPresent);
