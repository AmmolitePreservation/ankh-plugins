#pragma once

// NVIDIA's OpenGL driver takes over a window that covers its monitor exactly, as it does a fullscreen game, and switches
// an HDR monitor in and out of HDR as the game gains and loses the foreground; Direct3D games in the same borderless
// window are composed by Windows and never are. So an OpenGL game's frames are shown the Direct3D way: at each
// SwapBuffers its back buffer is copied, through the driver's OpenGL and Direct3D sharing (WGL_NV_DX_interop), into a
// Direct3D 11 texture and presented with a DXGI flip-model swap chain on the game's window, and the OpenGL present does
// not run. A driver without that sharing is fatal.

// Hooks the game's SwapBuffers. Called once from Load.
void installOpenGLPresenter();
