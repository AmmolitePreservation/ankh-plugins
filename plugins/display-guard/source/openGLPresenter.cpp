#include "openGLPresenter.h"

#include "fatalError.h"
#include "gameWindow.h"
#include "hookRegistry.h"
#include "sessionLog.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

namespace {

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;

const GLenum glReadFramebuffer = 0x8CA8;
const GLenum glDrawFramebuffer = 0x8CA9;
const GLenum glReadFramebufferBinding = 0x8CAA;
const GLenum glDrawFramebufferBinding = 0x8CA6;
const GLenum glTexture2D = 0x0DE1;
const GLenum glColorAttachment0 = 0x8CE0;
const GLenum glFramebufferComplete = 0x8CD5;
const GLbitfield glColorBufferBit = 0x4000;
const GLenum glNearest = 0x2600;
const GLenum glBack = 0x0405;
const GLenum glReadBufferState = 0x0C02;
const GLenum glScissorTest = 0x0C11;
const GLenum wglAccessWriteDiscard = 0x0002;

typedef BOOL (WINAPI* SwapBuffersFunction)(HDC);
typedef PROC (WINAPI* GetProcAddressGLFunction)(LPCSTR);
typedef HGLRC (WINAPI* GetCurrentContextFunction)();
typedef void (WINAPI* GetIntegervFunction)(GLenum, GLint*);
typedef GLboolean (WINAPI* IsEnabledFunction)(GLenum);
typedef void (WINAPI* CapabilityFunction)(GLenum);
typedef void (WINAPI* ReadBufferFunction)(GLenum);
typedef void (APIENTRY* GenerateFunction)(GLsizei, GLuint*);
typedef void (APIENTRY* BindFramebufferFunction)(GLenum, GLuint);
typedef void (APIENTRY* FramebufferTextureFunction)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (WINAPI* GenTexturesFunction)(GLsizei, GLuint*);
typedef GLenum (APIENTRY* CheckFramebufferStatusFunction)(GLenum);
typedef void (APIENTRY* BlitFramebufferFunction)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef HANDLE (WINAPI* DXOpenDeviceFunction)(void*);
typedef HANDLE (WINAPI* DXRegisterObjectFunction)(HANDLE, void*, GLuint, GLenum, GLenum);
typedef BOOL (WINAPI* DXLockObjectsFunction)(HANDLE, GLint, HANDLE*);
typedef HRESULT (WINAPI* CreateDeviceFunction)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
  UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

struct GLFunctions {
  GetIntegervFunction getIntegerv;
  IsEnabledFunction isEnabled;
  CapabilityFunction enable;
  CapabilityFunction disable;
  ReadBufferFunction readBuffer;
  GenerateFunction genFramebuffers;
  GenTexturesFunction genTextures;
  BindFramebufferFunction bindFramebuffer;
  FramebufferTextureFunction framebufferTexture2D;
  CheckFramebufferStatusFunction checkFramebufferStatus;
  BlitFramebufferFunction blitFramebuffer;
  DXOpenDeviceFunction dxOpenDevice;
  DXRegisterObjectFunction dxRegisterObject;
  DXLockObjectsFunction dxLockObjects;
  DXLockObjectsFunction dxUnlockObjects;
};

// Touched only on the game's render thread, the one that calls SwapBuffers with its context current.
SwapBuffersFunction originalSwapBuffers = nullptr;
GLFunctions gl = {};
HWND presentedWindow = nullptr;
UINT presentedWidth = 0;
UINT presentedHeight = 0;
ID3D11Device* device = nullptr;
ID3D11DeviceContext* deviceContext = nullptr;
IDXGISwapChain1* swapChain = nullptr;
ID3D11Texture2D* sharedTexture = nullptr;
HANDLE interopDevice = nullptr;
HANDLE interopTexture = nullptr;
GLuint framebuffer = 0;
GLuint sharedTextureName = 0;

void requireResult(HRESULT result, const char* what) {
  if (FAILED(result)) {
    fatalError("%s failed (0x%08lX)", what, static_cast<unsigned long>(result));
  }
}

template <typename Function>
Function openGLFunction(const char* name, bool isExtension) {
  const HMODULE openGL = GetModuleHandleA("opengl32.dll");
  if (openGL == nullptr) {
    fatalError("opengl32.dll is not loaded while the game presents with OpenGL");
  }
  PROC function = nullptr;
  if (isExtension) {
    const GetProcAddressGLFunction getProcAddress =
      reinterpret_cast<GetProcAddressGLFunction>(GetProcAddress(openGL, "wglGetProcAddress"));
    function = getProcAddress(name);
  } else {
    function = GetProcAddress(openGL, name);
  }
  if (function == nullptr) {
    fatalError("The graphics driver has no %s, which display-guard needs to show this OpenGL game the Direct3D way", name);
  }
  return reinterpret_cast<Function>(function);
}

void loadFunctions() {
  gl.getIntegerv = openGLFunction<GetIntegervFunction>("glGetIntegerv", false);
  gl.isEnabled = openGLFunction<IsEnabledFunction>("glIsEnabled", false);
  gl.enable = openGLFunction<CapabilityFunction>("glEnable", false);
  gl.disable = openGLFunction<CapabilityFunction>("glDisable", false);
  gl.readBuffer = openGLFunction<ReadBufferFunction>("glReadBuffer", false);
  gl.genFramebuffers = openGLFunction<GenerateFunction>("glGenFramebuffers", true);
  gl.genTextures = openGLFunction<GenTexturesFunction>("glGenTextures", false);
  gl.bindFramebuffer = openGLFunction<BindFramebufferFunction>("glBindFramebuffer", true);
  gl.framebufferTexture2D = openGLFunction<FramebufferTextureFunction>("glFramebufferTexture2D", true);
  gl.checkFramebufferStatus = openGLFunction<CheckFramebufferStatusFunction>("glCheckFramebufferStatus", true);
  gl.blitFramebuffer = openGLFunction<BlitFramebufferFunction>("glBlitFramebuffer", true);
  gl.dxOpenDevice = openGLFunction<DXOpenDeviceFunction>("wglDXOpenDeviceNV", true);
  gl.dxRegisterObject = openGLFunction<DXRegisterObjectFunction>("wglDXRegisterObjectNV", true);
  gl.dxLockObjects = openGLFunction<DXLockObjectsFunction>("wglDXLockObjectsNV", true);
  gl.dxUnlockObjects = openGLFunction<DXLockObjectsFunction>("wglDXUnlockObjectsNV", true);
}

void createDevice() {
  const HMODULE direct3D = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (direct3D == nullptr) {
    fatalError("d3d11.dll did not load (error %lu)", GetLastError());
  }
  const CreateDeviceFunction createDevice =
    reinterpret_cast<CreateDeviceFunction>(GetProcAddress(direct3D, "D3D11CreateDevice"));
  if (createDevice == nullptr) {
    fatalError("d3d11.dll has no D3D11CreateDevice");
  }
  requireResult(createDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
    D3D11_SDK_VERSION, &device, nullptr, &deviceContext), "D3D11CreateDevice");
  interopDevice = gl.dxOpenDevice(device);
  if (interopDevice == nullptr) {
    fatalError("wglDXOpenDeviceNV failed (error %lu): the graphics driver cannot share OpenGL with Direct3D 11", GetLastError());
  }
}

// The swap chain and the shared texture, at the window's back buffer size.
void createSurfaces(HWND window, UINT width, UINT height) {
  IDXGIDevice* dxgiDevice = nullptr;
  requireResult(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)), "IDXGIDevice");
  IDXGIAdapter* adapter = nullptr;
  requireResult(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
  IDXGIFactory2* factory = nullptr;
  requireResult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)), "IDXGIFactory2");
  DXGI_SWAP_CHAIN_DESC1 description = {};
  description.Width = width;
  description.Height = height;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.BufferCount = 2;
  description.Scaling = DXGI_SCALING_STRETCH;
  description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  requireResult(factory->CreateSwapChainForHwnd(device, window, &description, nullptr, nullptr, &swapChain),
    "CreateSwapChainForHwnd");
  requireResult(factory->MakeWindowAssociation(window, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER),
    "MakeWindowAssociation");
  factory->Release();
  adapter->Release();
  dxgiDevice->Release();

  D3D11_TEXTURE2D_DESC textureDescription = {};
  textureDescription.Width = width;
  textureDescription.Height = height;
  textureDescription.MipLevels = 1;
  textureDescription.ArraySize = 1;
  textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  textureDescription.SampleDesc.Count = 1;
  textureDescription.Usage = D3D11_USAGE_DEFAULT;
  textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  requireResult(device->CreateTexture2D(&textureDescription, nullptr, &sharedTexture), "CreateTexture2D");
  gl.genTextures(1, &sharedTextureName);
  interopTexture =
    gl.dxRegisterObject(interopDevice, sharedTexture, sharedTextureName, glTexture2D, wglAccessWriteDiscard);
  if (interopTexture == nullptr) {
    fatalError("wglDXRegisterObjectNV failed (error %lu)", GetLastError());
  }
  // OpenGL may use a shared object only while it is locked.
  HANDLE objects[] = { interopTexture };
  if (!gl.dxLockObjects(interopDevice, 1, objects)) {
    fatalError("wglDXLockObjectsNV failed (error %lu)", GetLastError());
  }
  GLint previousDraw = 0;
  gl.getIntegerv(glDrawFramebufferBinding, &previousDraw);
  gl.genFramebuffers(1, &framebuffer);
  gl.bindFramebuffer(glDrawFramebuffer, framebuffer);
  gl.framebufferTexture2D(glDrawFramebuffer, glColorAttachment0, glTexture2D, sharedTextureName, 0);
  const GLenum status = gl.checkFramebufferStatus(glDrawFramebuffer);
  gl.bindFramebuffer(glDrawFramebuffer, static_cast<GLuint>(previousDraw));
  if (!gl.dxUnlockObjects(interopDevice, 1, objects)) {
    fatalError("wglDXUnlockObjectsNV failed (error %lu)", GetLastError());
  }
  if (status != glFramebufferComplete) {
    fatalError("The shared texture's framebuffer is incomplete (0x%04X)", status);
  }
  presentedWindow = window;
  presentedWidth = width;
  presentedHeight = height;
  writeLogLine("INFO", "openGLPresentedThroughDXGI window=%p width=%u height=%u", static_cast<void*>(window), width, height);
}

// The game's back buffer into the shared texture, upside down: OpenGL's rows start at the bottom, Direct3D's at the top.
// The game's framebuffer bindings, read buffer and scissor test are put back.
void copyBackBuffer() {
  HANDLE objects[] = { interopTexture };
  if (!gl.dxLockObjects(interopDevice, 1, objects)) {
    fatalError("wglDXLockObjectsNV failed (error %lu)", GetLastError());
  }
  GLint previousRead = 0;
  GLint previousDraw = 0;
  GLint previousReadBuffer = 0;
  gl.getIntegerv(glReadFramebufferBinding, &previousRead);
  gl.getIntegerv(glDrawFramebufferBinding, &previousDraw);
  gl.getIntegerv(glReadBufferState, &previousReadBuffer);
  const bool wasScissorOn = gl.isEnabled(glScissorTest) != 0;
  gl.bindFramebuffer(glReadFramebuffer, 0);
  gl.readBuffer(glBack);
  gl.bindFramebuffer(glDrawFramebuffer, framebuffer);
  if (wasScissorOn) {
    gl.disable(glScissorTest);
  }
  const GLint width = static_cast<GLint>(presentedWidth);
  const GLint height = static_cast<GLint>(presentedHeight);
  gl.blitFramebuffer(0, 0, width, height, 0, height, width, 0, glColorBufferBit, glNearest);
  if (wasScissorOn) {
    gl.enable(glScissorTest);
  }
  gl.bindFramebuffer(glReadFramebuffer, static_cast<GLuint>(previousRead));
  gl.readBuffer(static_cast<GLenum>(previousReadBuffer));
  gl.bindFramebuffer(glDrawFramebuffer, static_cast<GLuint>(previousDraw));
  if (!gl.dxUnlockObjects(interopDevice, 1, objects)) {
    fatalError("wglDXUnlockObjectsNV failed (error %lu)", GetLastError());
  }
}

BOOL WINAPI swapBuffersDetour(HDC deviceContextHandle) {
  HWND window = nullptr;
  UINT width = 0;
  UINT height = 0;
  if (!gameBackBuffer(&window, &width, &height) || WindowFromDC(deviceContextHandle) != window) {
    return originalSwapBuffers(deviceContextHandle);
  }
  if (device == nullptr) {
    loadFunctions();
    createDevice();
  }
  if (presentedWindow != window || presentedWidth != width || presentedHeight != height) {
    if (swapChain != nullptr) {
      fatalError("The game's window or back buffer changed from %ux%u to %ux%u, which the Direct3D presentation does not "
        "follow", presentedWidth, presentedHeight, width, height);
    }
    createSurfaces(window, width, height);
  }
  copyBackBuffer();
  ID3D11Texture2D* backBuffer = nullptr;
  requireResult(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)), "GetBuffer");
  deviceContext->CopyResource(backBuffer, sharedTexture);
  backBuffer->Release();
  requireResult(swapChain->Present(0, 0), "IDXGISwapChain::Present");
  return TRUE;
}

}

void installOpenGLPresenter() {
  // Hooked for the whole process: a game may present through a library of its own, as SDL2 does.
  installExportHook("gdi32.dll", "SwapBuffers", reinterpret_cast<void*>(&swapBuffersDetour),
    reinterpret_cast<void**>(&originalSwapBuffers));
}
