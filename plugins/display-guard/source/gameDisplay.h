#pragma once

#include "pluginHost.h"

#include <windows.h>

// The game's own display settings and display code. The default leaves them alone; a game that reads its window mode
// or resolution from a settings file of its own, or whose code leaves fullscreen when it loses the foreground, replaces
// handlers/gameDisplay.cpp to fit them to a borderless window covering the monitor.

// Called once from Load, before any game code runs, with the rectangle of the monitor the game will cover.
void prepareGameDisplay(const PluginHost* host, const RECT& monitor);
