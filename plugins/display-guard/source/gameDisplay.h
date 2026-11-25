#pragma once

#include "pluginHost.h"

#include <windows.h>

// The game's own display settings and display code. The default leaves them alone; a game that reads its window mode
// or resolution from a settings file of its own, or whose code leaves fullscreen when it loses the foreground, replaces
// handlers/gameDisplay.cpp to fit them to a borderless window covering the monitor.

// Called once from Load, before any game code runs, with the rectangle of the monitor the game will cover.
void prepareGameDisplay(const PluginHost* host, const RECT& monitor);

// Some drivers hand a window that covers its monitor exactly the screen itself, as they do a fullscreen game, and switch
// the monitor in and out of HDR as it gains and loses the foreground. A guard keeps the window with the desktop:
// rowAbove makes it reach one row above the monitor (above, because OpenGL draws from the bottom of a window, so the
// game's picture still fills the monitor); layered makes it a layered window, which Windows always composes.
enum class TakeoverGuard { none, rowAbove, layered };

// The guard the game needs; the default is none. display-guard.cfg's takeoverGuard can name another.
TakeoverGuard gameTakeoverGuard();
