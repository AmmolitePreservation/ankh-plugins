#include "entryPointHook.h"
#include "fatalError.h"
#include "gameDefinition.h"
#include "hookRegistry.h"
#include "lateStart.h"
#include "linePipe.h"
#include "modulePath.h"
#include "pumpHooks.h"
#include "sessionLog.h"
#include "pluginHost.h"
#include "pluginLoading.h"
#include "systemProxy.h"

#include <windows.h>

#include <cstdio>
#include <string>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

static const char* const pluginName = "loader";
static const char* const loaderVersion = "0.1.0";
// Everything but the loader DLL lives here: the plugin list, the plugins with their settings and every log.
// The loader DLL itself has to sit beside the game's executable, the only place the game picks it up from.
static const char* const pluginsFolderName = "ankh-plugins";
static const char* const commandPipeBaseName = "ankh-plugins";

static HMODULE loaderModule = nullptr;
// One loader serves every game that imports the DLL it stands in for: which game this is comes from the install.
static GameDefinition game;
// Names this game session in every plugin's log file; plugins reloaded later keep using it.
static char sessionStamp[32];
static LinePipeServer commandPipe;
// Loaded by LoadLibrary after the entry point ran, as a packed executable does while it unpacks.
static bool isLateStart = false;

static void requireDirectory(const std::string& path, const char* description) {
  const DWORD attributes = GetFileAttributesA(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    fatalError("The %s folder is missing: %s", description, path.c_str());
  }
}

enum class ExecutableRole { game, companion, other };

// Every program in the game's folder that imports the DLL the loader stands in for loads this copy: only the game's
// executable and its companions (game.cfg) get the plugins.
static ExecutableRole executableRole() {
  char executablePath[MAX_PATH];
  const DWORD executablePathLength = GetModuleFileNameA(nullptr, executablePath, sizeof(executablePath));
  if (executablePathLength == 0 || executablePathLength >= sizeof(executablePath)) {
    fatalError("Could not resolve the executable path (error %lu)", GetLastError());
  }
  const char* executableName = strrchr(executablePath, '\\');
  executableName = executableName != nullptr ? executableName + 1 : executablePath;
  if (_stricmp(executableName, game.executableName.c_str()) == 0) {
    return ExecutableRole::game;
  }
  for (const std::string& companionName : game.companionNames) {
    if (_stricmp(executableName, companionName.c_str()) == 0) {
      return ExecutableRole::companion;
    }
  }
  return ExecutableRole::other;
}

// The files of another game in this folder would leave this game without its plugins and say nothing: its game.cfg
// names an executable that never starts here.
static void readGame() {
  const std::string clientFolder = resolveModuleDirectory(loaderModule);
  const std::string pluginsDirectory = clientFolder + "\\" + pluginsFolderName;
  game = readGameDefinition(pluginsDirectory);
  const std::string gameFile = gameDefinitionPath(pluginsDirectory, "game.cfg");
  if (_stricmp((game.loaderName + ".dll").c_str(), systemProxyLibraryName()) != 0) {
    fatalError("%s is for a game whose loader is %s.dll, but this loader is %s", gameFile.c_str(), game.loaderName.c_str(),
      systemProxyLibraryName());
  }
  if (GetFileAttributesA((clientFolder + "\\" + game.executableName).c_str()) == INVALID_FILE_ATTRIBUTES) {
    fatalError("%s is for the game %s, which is not in %s", gameFile.c_str(), game.executableName.c_str(),
      clientFolder.c_str());
  }
}

static const IMAGE_NT_HEADERS* executableHeaders() {
  const BYTE* executableBase = reinterpret_cast<const BYTE*>(GetModuleHandleA(nullptr));
  return reinterpret_cast<const IMAGE_NT_HEADERS*>(
    executableBase + reinterpret_cast<const IMAGE_DOS_HEADER*>(executableBase)->e_lfanew);
}

static void logExecutable() {
  char executablePath[MAX_PATH];
  const DWORD executablePathLength = GetModuleFileNameA(nullptr, executablePath, sizeof(executablePath));
  if (executablePathLength == 0 || executablePathLength >= sizeof(executablePath)) {
    fatalError("Could not resolve the game executable path (error %lu)", GetLastError());
  }
  const IMAGE_NT_HEADERS* headers = executableHeaders();
  // Not the command line: a launcher may put a login ticket on it.
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s mainThreadID=%lu executable=%s timeDateStamp=0x%08lX "
    "sizeOfImage=0x%08lX imageBase=%p", loaderVersion, ankhPluginsBuildCommit, GetCurrentThreadId(), executablePath,
    headers->FileHeader.TimeDateStamp, headers->OptionalHeader.SizeOfImage,
    static_cast<const void*>(GetModuleHandleA(nullptr)));
}

// A game's handlers hold addresses and code bytes of one build of its executable (game.cfg's timeDateStamp); in any
// other build they would patch the wrong code. A companion is another program with a build of its own, and a game
// whose game.cfg names no build gets the plugins in any.
static void requireGameBuild() {
  const DWORD found = executableHeaders()->FileHeader.TimeDateStamp;
  if (game.namesBuild && executableRole() == ExecutableRole::game && found != game.timeDateStamp) {
    fatalError("This is not the build of the game these plugins were made for: the executable's timeDateStamp is "
      "0x%08lX, not 0x%08lX", found, game.timeDateStamp);
  }
}

// Runs on the main thread outside the loader lock: at the game's entry point, or after a late load at the unpacked
// game's first startup call.
static void runLoader() {
  const std::string clientFolder = resolveModuleDirectory(loaderModule);
  const std::string pluginsDirectory = clientFolder + "\\" + pluginsFolderName;
  requireDirectory(pluginsDirectory, pluginsFolderName);
  SYSTEMTIME now;
  GetLocalTime(&now);
  snprintf(sessionStamp, sizeof(sessionStamp), "%04u%02u%02u_%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
    now.wMinute, now.wSecond);
  openSessionLog(pluginsDirectory, pluginName, sessionStamp);
  logExecutable();
  requireGameBuild();
  if (isLateStart) {
    writeLogLine("INFO", "startMode mode=late trigger=%s", lateStartTrigger());
  } else {
    writeLogLine("INFO", "startMode mode=entryPoint");
    initializeHookRegistry();
  }
  installPumpHooks(GetCurrentThreadId());
  const PluginHost host = { pluginsDirectory.c_str(), sessionStamp, GetCurrentThreadId(), &addPumpCallback,
    &removePumpCallback, &registerCommand };
  loadListedPlugins(host);
  commandPipe.startForProcess(commandPipeBaseName, &runLoaderCommand);
  writeLogLine("INFO", "loadComplete");
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    setFatalErrorTitle(pluginName);
    loaderModule = instance;
    DisableThreadLibraryCalls(instance);
    loadSystemProxyLibrary();
    readGame();
    if (executableRole() == ExecutableRole::other) {
      return TRUE;
    }
    // A null reserved pointer means LoadLibrary at runtime: the entry point has already run, so the plugins start at the
    // unpacked game's first startup call instead.
    if (reserved == nullptr) {
      isLateStart = true;
      initializeHookRegistry();
      installLateStart(&runLoader);
    } else {
      installEntryPointHook(&runLoader);
    }
  }
  return TRUE;
}
