#pragma once

#include <string>

struct Configuration {
  unsigned long foregroundFramesPerSecond;
  unsigned long backgroundFramesPerSecond;
};

// Reads frame-control.cfg from the ankh-plugins folder. Any problem is fatal.
Configuration loadConfiguration(const std::string& pluginDirectory);
