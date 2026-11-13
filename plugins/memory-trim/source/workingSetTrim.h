#pragma once

#include "configuration.h"
#include "pluginHost.h"

// Hands the game's working set back to Windows at the configured intervals, from the game's message pump.
void installWorkingSetTrim(const Configuration& configuration, const PluginHost* host);
