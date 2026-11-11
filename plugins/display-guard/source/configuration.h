#pragma once

#include <string>

enum class RenderResolution { native, game };

struct Configuration {
  std::string monitor;
  RenderResolution renderResolution;
  bool startMinimized;
  bool releaseInput;
};

// Reads display-guard.cfg from the ankh-plugins folder. Any problem is fatal.
Configuration loadConfiguration(const std::string& pluginDirectory);
