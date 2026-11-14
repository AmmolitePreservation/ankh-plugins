#pragma once

#include "pluginHost.h"

#include <windows.h>

#include <functional>
#include <string>

// Runs work on the game's main thread. The game pumps window messages every frame; the loader's
// pump callback runs queued work when the main thread enters a message-pump call.
void installMainThreadQueue(const PluginHost& host);

// Stops running queued work, once a run in progress has returned. No task may still be waiting.
void removeMainThreadQueue();

// Queues work and waits for the main thread to run it, putting its reply in result. Returns false,
// with the work withdrawn unrun, when the main thread did not start it within waitMilliseconds.
bool runOnMainThread(const std::function<std::string()>& work, unsigned long waitMilliseconds, std::string* result);
