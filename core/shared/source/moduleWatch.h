#pragma once

// Runs the callback once when the named module (such as "d3d9.dll") is in the process: at once when it is already
// loaded, otherwise as it loads, on the loading thread and under the loader lock. The module then stays loaded until
// the process ends, whatever the game frees, so what the callback hooked in it stays hooked. Any failure is fatal.
void whenModuleLoads(const char* moduleName, void (*callback)());
