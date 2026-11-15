#pragma once

// Runs the callback once when the named module (such as "d3d9.dll") is in the process: at once when it is already
// loaded, otherwise as it loads, on the loading thread and under the loader lock. The module then stays loaded until
// the process ends, whatever the game frees, so what the callback hooked in it stays hooked. Any failure is fatal.
void whenModuleLoads(const char* moduleName, void (*callback)());

// Runs onLoad each time the named module is loaded (at once when it is loaded already) and onUnload each time it is
// freed, before Windows unmaps it; both on the loading or freeing thread, under the loader lock. The module is not
// kept loaded: for a module whose unloading others in the process act on, as a game's own program or another tool
// may. onUnload takes back what onLoad put into the module (removeHook). Any failure is fatal.
void watchModuleLoads(const char* moduleName, void (*onLoad)(), void (*onUnload)());
