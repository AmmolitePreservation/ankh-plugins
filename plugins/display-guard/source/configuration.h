#pragma once

#include <string>
#include <vector>

enum class RenderResolution { native, game };

struct Configuration {
  std::string monitor;
  RenderResolution renderResolution;
  bool startMinimized;
  bool releaseInput;
  bool confineCursor;
  std::vector<std::string> windowModules;
};

// Reads display-guard.cfg from the ankh-plugins folder. Any problem is fatal.
Configuration loadConfiguration(const std::string& pluginDirectory);
