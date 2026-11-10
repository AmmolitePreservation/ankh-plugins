#pragma once

#include "pluginHost.h"

#include <string>

// Loads every DLL named in <host.pluginDirectory>\plugins.txt, in file order, each from a shadow copy under
// <host.pluginDirectory>\loaded, and calls its Load export with the host on the current (main) thread. Shadow copies
// left by games that have ended are removed first. The list is written from the loader's embedded default when
// missing and brought up to this release's plugins (readPluginList). Any other problem is fatal.
void loadListedPlugins(const PluginHost& host);

// PluginHost.registerCommand.
void registerCommand(const char* name, PluginCommandFunction handler);

// The control pipe's commands, one line each:
//   plugins      -> ok <dll>=<generation> ... reloadable=<dll>,<dll>
//   reload <dll> -> ok generation=<n> milliseconds=<n>: Unload, free, then load the DLL now in the
//                   ankh-plugins folder from a fresh shadow copy
//   <name> ...   -> the reply of the plugin that registered <name>
std::string runLoaderCommand(const std::string& line);
