#pragma once

#include <windows.h>

// Hooks the message-pump calls (PeekMessageA/W, GetMessageA/W) so a plugin can run work on the game's
// main thread. The hooks live in the loader, which is never unloaded, because the main thread spends
// much of its time inside them; a reloaded plugin only removes and adds its callback.
void installPumpHooks(DWORD mainThreadID);

// PluginHost::addPumpCallback and PluginHost::removePumpCallback.
void addPumpCallback(void (*callback)());
void removePumpCallback(void (*callback)());
