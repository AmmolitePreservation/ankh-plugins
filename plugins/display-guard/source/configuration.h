#pragma once

#include "gameDisplay.h"

#include <string>

enum class RenderResolution { native, game };

struct Configuration {
  std::string monitor;
  RenderResolution renderResolution;
  bool startMinimized;
  bool releaseInput;
  bool confineCursor;
  TakeoverGuard takeoverGuard;
};

const char* takeoverGuardName(TakeoverGuard guard);

// Reads display-guard.cfg from the ankh-plugins folder. Any problem is fatal.
Configuration loadConfiguration(const std::string& pluginDirectory);
