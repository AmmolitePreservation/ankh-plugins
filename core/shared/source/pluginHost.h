#pragma once

#include <windows.h>

// Changes with any change to this header that an already built plugin would not survive.
const DWORD pluginHostInterfaceVersion = 1;

// A plugin's handler for one control-pipe command: argument is the rest of the command's line. It writes its reply,
// one line in the loader's ok/error form, into reply and returns the number of bytes written, at most replyCapacity,
// or a negative value on failure. It runs on the control-pipe thread, not the game's main thread: work that has to
// run there goes through a pump callback.
typedef int (*PluginCommandFunction)(const char* argument, char* reply, int replyCapacity);

// What the loader hands each plugin's Load. Every plugin exports
//   DWORD PluginInterfaceVersion(void)
//   DWORD Load(const PluginHost* host)
// and a plugin that also exports
//   DWORD Unload(void)
// can be reloaded while the game runs: Unload removes everything the plugin installed, stops its
// threads, and returns once no thread can run the plugin's code again, so the loader can free it.
// Plugins load from shadow copies under <pluginDirectory>\loaded, so a build can replace any plugin's DLL
// while the game holds it.
struct PluginHost {
  // The ankh-plugins folder: the plugin list, the plugins, their settings files and the log folders.
  const char* pluginDirectory;
  // Names this game session in every plugin's log file name; the same across reloads.
  const char* sessionStamp;
  DWORD mainThreadID;
  // Runs callback on the game's main thread each time it pumps window messages (PeekMessage,
  // GetMessage), along with every other plugin's. Adding one already added, or more than the loader
  // holds, is fatal.
  void (*addPumpCallback)(void (*callback)());
  // Stops a callback added before, returning once a run in progress has returned; one not added is
  // fatal.
  void (*removePumpCallback)(void (*callback)());
  // Sends every control-pipe line whose first word is name to handler. Only during Load; a name the loader or
  // another plugin already uses is fatal. A reload drops the plugin's commands before its new Load.
  void (*registerCommand)(const char* name, PluginCommandFunction handler);
};

typedef DWORD (*PluginInterfaceVersionFunction)(void);
typedef DWORD (*PluginLoadFunction)(const PluginHost* host);
typedef DWORD (*PluginUnloadFunction)(void);
