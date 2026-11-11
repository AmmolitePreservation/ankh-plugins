#include "pluginLoading.h"

#include "defaultFiles.h"
#include "fatalError.h"
#include "sessionLog.h"
#include "settingsFile.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const char* const pluginListFileName = "plugins.txt";
static const char* const shadowFolderName = "loaded";
static const char dllExtension[] = ".dll";

struct LoadedPlugin {
  std::string fileName;
  unsigned long generation;
  std::string shadowPath;
  HMODULE module;
  PluginUnloadFunction unload;
};

static PluginHost pluginHost;
static std::string pluginDirectory;
static std::string shadowDirectory;
static std::vector<LoadedPlugin> loadedPlugins;

struct PluginCommand {
  std::string name;
  std::string pluginFileName;
  PluginCommandFunction handler;
};

static const char* const loaderCommandNames[] = { "plugins", "reload" };
static const int commandReplyCapacity = 65536;
// No lock: the commands change only during a Load, which runs on the main thread before the control pipe starts or on
// the pipe's thread during a reload, and the pipe's thread is the only one that runs them.
static std::vector<PluginCommand> pluginCommands;
// The plugin whose Load is running; empty outside one.
static std::string loadingPluginFileName;

static const PluginCommand* findCommand(const std::string& name) {
  for (const PluginCommand& command : pluginCommands) {
    if (command.name == name) {
      return &command;
    }
  }
  return nullptr;
}

void registerCommand(const char* name, PluginCommandFunction handler) {
  if (loadingPluginFileName.empty()) {
    fatalError("A plugin registered the command '%s' outside its Load", name != nullptr ? name : "");
  }
  const char* pluginFileName = loadingPluginFileName.c_str();
  if (name == nullptr || handler == nullptr) {
    fatalError("%s registered a command without a name or a handler", pluginFileName);
  }
  if (name[0] == '\0' || strpbrk(name, " \t\r\n") != nullptr) {
    fatalError("%s registered the command '%s', but a command's name is one word", pluginFileName, name);
  }
  for (const char* loaderCommandName : loaderCommandNames) {
    if (strcmp(name, loaderCommandName) == 0) {
      fatalError("%s registered the command '%s', which is the loader's own", pluginFileName, name);
    }
  }
  if (const PluginCommand* taken = findCommand(name)) {
    fatalError("%s registered the command '%s', which %s registered already", pluginFileName, name,
      taken->pluginFileName.c_str());
  }
  pluginCommands.push_back({ name, loadingPluginFileName, handler });
  writeLogLine("INFO", "commandRegistered name=%s plugin=%s", name, pluginFileName);
}

static bool hasDllExtension(const std::string& name) {
  const size_t extensionLength = sizeof(dllExtension) - 1;
  return name.size() > extensionLength &&
    _stricmp(name.c_str() + name.size() - extensionLength, dllExtension) == 0;
}

static bool isPluginFileName(const std::string& name) {
  return name.find_first_of("\\/: ") == std::string::npos && hasDllExtension(name);
}

struct PluginEntry {
  std::string fileName;
  bool isOn;
  size_t lineIndex;
};

enum class PluginLineKind {
  blankOrComment,
  plugin,
  unusable,
};

static bool isInPluginDirectory(const std::string& fileName);

// A line names a plugin, switched off when it starts with #; any other line starting with # is a comment, and any other
// line is unusable.
static PluginLineKind readPluginEntry(const std::string& rawLine, size_t lineIndex, PluginEntry* entry) {
  const std::string line = trimWhitespace(rawLine);
  if (line.empty()) {
    return PluginLineKind::blankOrComment;
  }
  if (line[0] != '#') {
    if (!isPluginFileName(line)) {
      return PluginLineKind::unusable;
    }
    *entry = { line, true, lineIndex };
    return PluginLineKind::plugin;
  }
  const std::string name = trimWhitespace(line.substr(1));
  if (!isPluginFileName(name)) {
    return PluginLineKind::blankOrComment;
  }
  *entry = { name, false, lineIndex };
  return PluginLineKind::plugin;
}

static const PluginEntry* findEntry(const std::vector<PluginEntry>& entries, const std::string& fileName) {
  for (const PluginEntry& entry : entries) {
    if (_stricmp(entry.fileName.c_str(), fileName.c_str()) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

// The plugins to load, in list order. Every plugin the shipped list names stays in the file, on or switched off: one the
// file lacks, such as a plugin a newer release added, goes in with its shipped state after the nearest plugin listed
// before it there, so the load order stays as shipped. A line naming a plugin the release does not ship and the folder
// does not hold, such as one a newer release dropped, is removed and logged, and so is a line that names no plugin.
// Each plugin is a package of its own, so a listed plugin whose DLL is not in the folder is simply not installed: it
// keeps its line and is not loaded.
static std::vector<std::string> readPluginList() {
  const std::string listPath = pluginDirectory + "\\" + pluginListFileName;
  writeDefaultFileIfMissing(listPath, pluginListFileName);

  const DefaultFile& shippedList = findDefaultFile(pluginListFileName);
  const std::vector<std::string> shippedLines = splitLines(std::string(shippedList.contents, shippedList.size));
  std::vector<PluginEntry> shippedEntries;
  for (size_t index = 0; index < shippedLines.size(); index++) {
    PluginEntry entry;
    if (readPluginEntry(shippedLines[index], index, &entry) == PluginLineKind::plugin) {
      shippedEntries.push_back(entry);
    }
  }

  std::string contents;
  const std::string readProblem = readWholeFile(listPath, pluginListFileName, contents);
  if (!readProblem.empty()) {
    fatalError("%s", readProblem.c_str());
  }
  const std::vector<std::string> fileLines = splitLines(contents);
  std::vector<std::string> lines;
  std::vector<PluginEntry> entries;
  bool isChanged = false;
  for (size_t index = 0; index < fileLines.size(); index++) {
    PluginEntry entry;
    const PluginLineKind kind = readPluginEntry(fileLines[index], lines.size(), &entry);
    if (kind == PluginLineKind::unusable || (kind == PluginLineKind::plugin &&
        findEntry(shippedEntries, entry.fileName) == nullptr && !isInPluginDirectory(entry.fileName))) {
      writeLogLine("WARN", "pluginRemoved file=%s line=%zu text=%s", pluginListFileName, index + 1,
        trimWhitespace(fileLines[index]).c_str());
      isChanged = true;
      continue;
    }
    if (kind == PluginLineKind::plugin) {
      if (findEntry(entries, entry.fileName) != nullptr) {
        fatalError("%s line %zu: %s is listed twice", pluginListFileName, index + 1, entry.fileName.c_str());
      }
      entries.push_back(entry);
    }
    lines.push_back(fileLines[index]);
  }

  for (size_t shippedIndex = 0; shippedIndex < shippedEntries.size(); shippedIndex++) {
    const PluginEntry& shipped = shippedEntries[shippedIndex];
    if (findEntry(entries, shipped.fileName) != nullptr) {
      continue;
    }
    size_t insertAt = !lines.empty() && lines.back().empty() ? lines.size() - 1 : lines.size();
    if (!entries.empty()) {
      insertAt = entries.front().lineIndex;
      for (const PluginEntry& entry : entries) {
        insertAt = entry.lineIndex < insertAt ? entry.lineIndex : insertAt;
      }
    }
    for (size_t earlier = shippedIndex; earlier > 0; earlier--) {
      const PluginEntry* listed = findEntry(entries, shippedEntries[earlier - 1].fileName);
      if (listed != nullptr) {
        insertAt = listed->lineIndex + 1;
        break;
      }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), (shipped.isOn ? "" : "#") + shipped.fileName);
    for (PluginEntry& entry : entries) {
      entry.lineIndex += entry.lineIndex >= insertAt ? 1 : 0;
    }
    entries.push_back({ shipped.fileName, shipped.isOn, insertAt });
    writeLogLine("INFO", "pluginAdded file=%s plugin=%s on=%s", pluginListFileName, shipped.fileName.c_str(),
      shipped.isOn ? "true" : "false");
    isChanged = true;
  }
  if (isChanged) {
    std::string newContents;
    for (size_t index = 0; index < lines.size(); index++) {
      newContents += lines[index] + (index + 1 < lines.size() ? "\r\n" : "");
    }
    replaceFile(listPath, newContents);
  }

  std::vector<std::string> pluginNames;
  for (size_t index = 0; index < lines.size(); index++) {
    for (const PluginEntry& entry : entries) {
      if (entry.lineIndex != index || !entry.isOn) {
        continue;
      }
      if (isInPluginDirectory(entry.fileName)) {
        pluginNames.push_back(entry.fileName);
      } else {
        writeLogLine("INFO", "pluginNotInstalled plugin=%s", entry.fileName.c_str());
      }
    }
  }
  return pluginNames;
}

// The DLL loader takes long paths only through the wide calls, even with the process marked long-path aware.
static std::wstring widePath(const std::string& path) {
  const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
  if (length <= 0) {
    fatalError("Could not convert the path %s (error %lu)", path.c_str(), GetLastError());
  }
  std::wstring converted(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path.c_str(), static_cast<int>(path.size()), &converted[0], length);
  return converted;
}

static bool isInPluginDirectory(const std::string& fileName) {
  return GetFileAttributesA((pluginDirectory + "\\" + fileName).c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::string shadowPathFor(const std::string& fileName, unsigned long generation) {
  const std::string stem = fileName.substr(0, fileName.size() - (sizeof(dllExtension) - 1));
  char suffix[64];
  snprintf(suffix, sizeof(suffix), ".%lu.%lu%s", GetCurrentProcessId(), generation, dllExtension);
  return shadowDirectory + "\\" + stem + suffix;
}

// The process ID in a shadow copy's name, <plugin>.<process ID>.<generation>.dll; zero for any other name.
static DWORD processOfShadowCopy(const std::string& name) {
  const size_t stemEnd = name.size() - (sizeof(dllExtension) - 1);
  const size_t generationDot = name.rfind('.', stemEnd - 1);
  if (generationDot == std::string::npos || generationDot == 0) {
    return 0;
  }
  const size_t processDot = name.rfind('.', generationDot - 1);
  if (processDot == std::string::npos) {
    return 0;
  }
  const std::string digits = name.substr(processDot + 1, generationDot - processDot - 1);
  if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) {
    return 0;
  }
  return strtoul(digits.c_str(), nullptr, 10);
}

static bool isProcessRunning(DWORD processID) {
  const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processID);
  if (process == nullptr) {
    return GetLastError() == ERROR_ACCESS_DENIED;
  }
  const bool isRunning = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
  CloseHandle(process);
  return isRunning;
}

// Shadow copies are named <plugin>.<process ID>.<generation>.dll. Only the copies of games that have ended are
// removed: a game starting at the same moment holds its fresh copies open only once it has loaded them.
static void prepareShadowDirectory() {
  shadowDirectory = pluginDirectory + "\\" + shadowFolderName;
  if (!CreateDirectoryA(shadowDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    fatalError("Could not create the %s folder for the plugins' shadow copies (error %lu)", shadowFolderName, GetLastError());
  }
  const std::string searchPattern = shadowDirectory + "\\*" + dllExtension;
  WIN32_FIND_DATAA foundFile;
  HANDLE searchHandle = FindFirstFileA(searchPattern.c_str(), &foundFile);
  if (searchHandle == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
      return;
    }
    fatalError("Could not list the shadow copies (error %lu)", GetLastError());
  }
  do {
    const std::string path = shadowDirectory + "\\" + foundFile.cFileName;
    const DWORD owner = processOfShadowCopy(foundFile.cFileName);
    if (owner != 0 && isProcessRunning(owner)) {
      writeLogLine("INFO", "shadowCopyKept file=%s reason=running", foundFile.cFileName);
    } else if (DeleteFileA(path.c_str())) {
      writeLogLine("INFO", "shadowCopyRemoved file=%s", foundFile.cFileName);
    } else {
      writeLogLine("INFO", "shadowCopyKept file=%s error=%lu", foundFile.cFileName, GetLastError());
    }
  } while (FindNextFileA(searchHandle, &foundFile));
  const DWORD enumerationError = GetLastError();
  FindClose(searchHandle);
  if (enumerationError != ERROR_NO_MORE_FILES) {
    fatalError("Listing the shadow copies stopped early (error %lu)", enumerationError);
  }
}

// Copies the plugin's DLL to a fresh shadow copy, loads that, and calls Load, whose own failures are fatal inside the
// plugin. Every other failure is fatal here, a plugin built for another plugin interface included.
static LoadedPlugin loadPlugin(const std::string& fileName, unsigned long generation) {
  const std::string sourcePath = pluginDirectory + "\\" + fileName;
  const std::string shadowPath = shadowPathFor(fileName, generation);
  if (!CopyFileA(sourcePath.c_str(), shadowPath.c_str(), FALSE)) {
    fatalError("Could not copy %s to its shadow copy (error %lu)", fileName.c_str(), GetLastError());
  }
  HMODULE module = LoadLibraryExW((L"\\\\?\\" + widePath(shadowPath)).c_str(), nullptr, 0);
  if (module == nullptr) {
    fatalError("Could not load %s (error %lu)", fileName.c_str(), GetLastError());
  }
  const PluginInterfaceVersionFunction interfaceVersion =
    reinterpret_cast<PluginInterfaceVersionFunction>(GetProcAddress(module, "PluginInterfaceVersion"));
  if (interfaceVersion == nullptr) {
    fatalError("%s does not export PluginInterfaceVersion", fileName.c_str());
  }
  if (interfaceVersion() != pluginHostInterfaceVersion) {
    fatalError("%s is built for plugin interface %lu, but this loader provides %lu", fileName.c_str(), interfaceVersion(),
      pluginHostInterfaceVersion);
  }
  PluginLoadFunction load = reinterpret_cast<PluginLoadFunction>(GetProcAddress(module, "Load"));
  if (load == nullptr) {
    fatalError("%s does not export Load", fileName.c_str());
  }
  const DWORD startTicks = GetTickCount();
  loadingPluginFileName = fileName;
  const DWORD result = load(&pluginHost);
  loadingPluginFileName.clear();
  if (result != 0) {
    fatalError("%s Load returned %lu", fileName.c_str(), result);
  }
  const LoadedPlugin loaded = { fileName, generation, shadowPath, module,
    reinterpret_cast<PluginUnloadFunction>(GetProcAddress(module, "Unload")) };
  writeLogLine("INFO", "pluginLoaded name=%s generation=%lu reloadable=%s milliseconds=%lu", fileName.c_str(), generation,
    loaded.unload != nullptr ? "true" : "false", GetTickCount() - startTicks);
  return loaded;
}

void loadListedPlugins(const PluginHost& host) {
  pluginDirectory = host.pluginDirectory;
  pluginHost = host;
  pluginHost.pluginDirectory = pluginDirectory.c_str();
  prepareShadowDirectory();
  const std::vector<std::string> pluginNames = readPluginList();
  if (pluginNames.empty()) {
    writeLogLine("WARN", "noPluginsListed file=%s", pluginListFileName);
    return;
  }
  for (const std::string& pluginName : pluginNames) {
    if (!isInPluginDirectory(pluginName)) {
      fatalError("%s lists %s, but it is not in the ankh-plugins folder", pluginListFileName, pluginName.c_str());
    }
    loadedPlugins.push_back(loadPlugin(pluginName, 1));
  }
}

static std::string reloadPlugin(const std::string& fileName) {
  LoadedPlugin* plugin = nullptr;
  for (LoadedPlugin& loaded : loadedPlugins) {
    if (_stricmp(loaded.fileName.c_str(), fileName.c_str()) == 0) {
      plugin = &loaded;
    }
  }
  if (plugin == nullptr) {
    return "error " + fileName + " is not a loaded plugin";
  }
  if (plugin->unload == nullptr) {
    return "error " + plugin->fileName + " does not export Unload, so it loads only at game start";
  }
  if (!isInPluginDirectory(plugin->fileName)) {
    return "error " + plugin->fileName + " is not in the ankh-plugins folder";
  }
  const DWORD startTicks = GetTickCount();
  const DWORD unloadResult = plugin->unload();
  if (unloadResult != 0) {
    fatalError("%s Unload returned %lu", plugin->fileName.c_str(), unloadResult);
  }
  pluginCommands.erase(std::remove_if(pluginCommands.begin(), pluginCommands.end(),
    [plugin](const PluginCommand& command) { return command.pluginFileName == plugin->fileName; }), pluginCommands.end());
  if (!FreeLibrary(plugin->module)) {
    fatalError("Could not free %s (error %lu)", plugin->fileName.c_str(), GetLastError());
  }
  HMODULE stillLoaded = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(plugin->module), &stillLoaded)) {
    fatalError("%s is still loaded after Unload and FreeLibrary; something else holds a reference to it", plugin->fileName.c_str());
  }
  if (!DeleteFileA(plugin->shadowPath.c_str())) {
    fatalError("Could not remove the unloaded shadow copy of %s (error %lu)", plugin->fileName.c_str(), GetLastError());
  }
  writeLogLine("INFO", "pluginUnloaded name=%s generation=%lu", plugin->fileName.c_str(), plugin->generation);
  *plugin = loadPlugin(plugin->fileName, plugin->generation + 1);
  return "ok generation=" + std::to_string(plugin->generation) + " milliseconds=" + std::to_string(GetTickCount() - startTicks);
}

static std::string listPlugins() {
  std::string reply = "ok";
  std::string reloadable;
  for (const LoadedPlugin& plugin : loadedPlugins) {
    reply += " " + plugin.fileName + "=" + std::to_string(plugin.generation);
    if (plugin.unload != nullptr) {
      reloadable += (reloadable.empty() ? "" : ",") + plugin.fileName;
    }
  }
  return reply + " reloadable=" + reloadable;
}

static std::string runPluginCommand(const PluginCommand& command, const std::string& argument) {
  std::vector<char> reply(static_cast<size_t>(commandReplyCapacity));
  const int length = command.handler(argument.c_str(), reply.data(), commandReplyCapacity);
  if (length > commandReplyCapacity) {
    fatalError("%s's command '%s' reported a reply of %d bytes in a buffer of %d", command.pluginFileName.c_str(),
      command.name.c_str(), length, commandReplyCapacity);
  }
  if (length < 0) {
    return "error command '" + command.name + "' failed (" + std::to_string(length) + ")";
  }
  const std::string text(reply.data(), static_cast<size_t>(length));
  if (text.find_first_of("\r\n") != std::string::npos) {
    return "error command '" + command.name + "' replied with more than one line";
  }
  return text;
}

std::string runLoaderCommand(const std::string& line) {
  const size_t space = line.find(' ');
  const std::string name = line.substr(0, space);
  const std::string argument = space == std::string::npos ? "" : line.substr(space + 1);
  std::string reply;
  if (name == "plugins") {
    reply = argument.empty() ? listPlugins() : "error plugins takes no arguments";
  } else if (name == "reload") {
    reply = argument.empty() || argument.find(' ') != std::string::npos
      ? "error reload takes one plugin DLL name" : reloadPlugin(argument);
  } else if (const PluginCommand* command = findCommand(name)) {
    reply = runPluginCommand(*command, argument);
  } else {
    reply = "error unknown command '" + name + "'";
  }
  // A log line holds 1 KB and a longer one is fatal, while a command line or a reply can be far longer.
  const size_t loggedLength = 160;
  const auto bounded = [loggedLength](const std::string& text) {
    return text.size() > loggedLength
      ? text.substr(0, loggedLength) + "...(" + std::to_string(text.size()) + " bytes)" : text;
  };
  writeLogLine(reply.compare(0, 2, "ok") == 0 ? "INFO" : "WARN", "command line=%s reply=%s", bounded(line).c_str(),
    bounded(reply).c_str());
  return reply;
}
