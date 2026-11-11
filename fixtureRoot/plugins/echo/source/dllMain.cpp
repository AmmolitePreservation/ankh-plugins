#include "fatalError.h"
#include "handlerName.h"
#include "pluginHost.h"
#include "sessionLog.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifndef ankhPluginsBuildCommit
#error "ankhPluginsBuildCommit must be defined by the build (see build.ps1)"
#endif

// The plugin the loader's tests load into the fixture game: its commands exercise every way the loader answers a
// plugin command, its pump callback shows which thread the loader runs callbacks on, and its one handler shows whether
// the build took the game's file for it, with that file's own source and settings files.

static const char* const pluginName = "echo";
static const char* const echoVersion = "0.1.0";

static bool isLoaded = false;
static DWORD gameMainThreadID = 0;
static void (*removePumpCallback)(void (*callback)()) = nullptr;
static volatile LONG pumpCount = 0;
static volatile LONG lastPumpThreadID = 0;
static const char* handlerGreeting = nullptr;

static void countPump() {
  InterlockedIncrement(&pumpCount);
  InterlockedExchange(&lastPumpThreadID, static_cast<LONG>(GetCurrentThreadId()));
}

// The number of bytes written, or -1 when the reply does not fit.
static int formatReply(char* reply, int replyCapacity, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(reply, static_cast<size_t>(replyCapacity), format, arguments);
  va_end(arguments);
  return length >= 0 && length < replyCapacity ? length : -1;
}

static int echoCommand(const char* argument, char* reply, int replyCapacity) {
  return formatReply(reply, replyCapacity, "ok text=%s", argument);
}

static int pumpCountCommand(const char*, char* reply, int replyCapacity) {
  return formatReply(reply, replyCapacity, "ok count=%ld threadID=%lu", InterlockedCompareExchange(&pumpCount, 0, 0),
    static_cast<DWORD>(InterlockedCompareExchange(&lastPumpThreadID, 0, 0)));
}

static int quitCommand(const char*, char* reply, int replyCapacity) {
  if (!PostThreadMessageA(gameMainThreadID, WM_QUIT, 0, 0)) {
    return formatReply(reply, replyCapacity, "error PostThreadMessage failed (error %lu)", GetLastError());
  }
  return formatReply(reply, replyCapacity, "ok");
}

static int failCommand(const char*, char*, int) {
  return -7;
}

static int twoLinesCommand(const char*, char* reply, int replyCapacity) {
  return formatReply(reply, replyCapacity, "ok first\nsecond");
}

// Fills the reply buffer to its last byte, with no terminator.
static int longCommand(const char*, char* reply, int replyCapacity) {
  static const char prefix[] = "ok text=";
  const size_t prefixLength = sizeof(prefix) - 1;
  memcpy(reply, prefix, prefixLength);
  memset(reply + prefixLength, 'x', static_cast<size_t>(replyCapacity) - prefixLength);
  return replyCapacity;
}

static int handlerCommand(const char*, char* reply, int replyCapacity) {
  return formatReply(reply, replyCapacity, "ok name=%s greeting=%s", handlerName(), handlerGreeting);
}

extern "C" {

// The loader calls this on the game's main thread at game start, and on its control-pipe thread for a reload.
__declspec(dllexport) DWORD Load(const PluginHost* host) {
  if (isLoaded) {
    fatalError("Load was called twice");
  }
  isLoaded = true;
  setFatalErrorTitle(pluginName);
  openSessionLog(host->pluginDirectory, pluginName, host->sessionStamp);
  writeLogLine("INFO", "sessionStart version=%s buildCommit=%s mainThreadID=%lu", echoVersion, ankhPluginsBuildCommit,
    host->mainThreadID);
  gameMainThreadID = host->mainThreadID;
  removePumpCallback = host->removePumpCallback;
  handlerGreeting = loadHandlerGreeting(host->pluginDirectory);
  host->registerCommand("echo", &echoCommand);
  host->registerCommand("pumpCount", &pumpCountCommand);
  host->registerCommand("quit", &quitCommand);
  host->registerCommand("fail", &failCommand);
  host->registerCommand("twoLines", &twoLinesCommand);
  host->registerCommand("long", &longCommand);
  host->registerCommand("handler", &handlerCommand);
  host->addPumpCallback(&countPump);
  writeLogLine("INFO", "loadComplete");
  return 0;
}

__declspec(dllexport) DWORD Unload() {
  removePumpCallback(&countPump);
  writeLogLine("INFO", "unloadComplete");
  closeSessionLog();
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

}
