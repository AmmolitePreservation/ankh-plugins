#pragma once

#include "pluginHost.h"

#include <windows.h>

// The game's own display settings. The default leaves them alone; a game that reads its window mode or resolution from
// a settings file of its own replaces handlers/gameSettings.cpp to set them to what a borderless window covering the
// monitor needs.

// Called once from Load, before any game code runs, with the rectangle of the monitor the game will cover.
void prepareGameSettings(const PluginHost* host, const RECT& monitor);
