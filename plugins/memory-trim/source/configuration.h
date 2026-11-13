#pragma once

#include <string>

struct Configuration {
  unsigned long backgroundTrimSeconds;
  unsigned long foregroundTrimSeconds;
};

// Reads memory-trim.cfg from the ankh-plugins folder. Any problem is fatal.
Configuration loadConfiguration(const std::string& pluginDirectory);
