#pragma once

// The module mode: loads fixtureModule.dll from beside the executable, calls its export and frees it, twice, writing
// "moduleValue cycle=<n> value=<v>" and "moduleFreed cycle=<n> isLoaded=<0|1>" for each, then "ready". Returns 0, or
// main.cpp's exit code for a failure.
int runModuleMode();
