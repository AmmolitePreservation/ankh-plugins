#pragma once

#include "configuration.h"

#include <string>

// The fixes for faults in one game's own code, installed after the guard's own hooks. A game without a handler has
// none. pluginDirectory is where the handler's own settings file is read from.
void installGameFixes(const std::string& pluginDirectory, const Configuration& configuration);
