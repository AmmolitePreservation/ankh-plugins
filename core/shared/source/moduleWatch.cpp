#include "moduleWatch.h"

#include "fatalError.h"

#include <windows.h>
#include <winternl.h>

#include <cstring>
#include <cwctype>

struct DllNotificationData {
  ULONG flags;
  const UNICODE_STRING* fullDllName;
  const UNICODE_STRING* baseDllName;
  PVOID dllBase;
  ULONG sizeOfImage;
};

typedef VOID (CALLBACK* DllNotificationFunction)(ULONG reason, const DllNotificationData* data, PVOID context);
typedef NTSTATUS (NTAPI* LdrRegisterDllNotificationFunction)(ULONG flags, DllNotificationFunction function, PVOID context,
  PVOID* cookie);

static const ULONG dllLoadedReason = 1;
static const ULONG dllUnloadedReason = 2;
static const size_t moduleNameCapacity = 64;
static const LONG moduleWatchCapacity = 8;

// A pinned watch runs onLoad once and keeps the module; an unpinned one runs onLoad at each load and onUnload at each
// unload, state saying whether the module is loaded.
struct ModuleWatch {
  char moduleName[moduleNameCapacity];
  void (*onLoad)();
  void (*onUnload)();
  bool isPinned;
  volatile LONG state;
};

static ModuleWatch moduleWatches[moduleWatchCapacity];
static volatile LONG moduleWatchCount = 0;

static bool isNamed(const UNICODE_STRING* baseName, const char* moduleName) {
  const size_t length = baseName->Length / sizeof(wchar_t);
  if (length != strlen(moduleName)) {
    return false;
  }
  for (size_t index = 0; index < length; index++) {
    if (towlower(baseName->Buffer[index]) != towlower(static_cast<unsigned char>(moduleName[index]))) {
      return false;
    }
  }
  return true;
}

// A pinned watch's callback hooks a module that, freed and loaded again, would come back without the hooks: a game may
// load a library only to see whether it is there. Pinned, it stays for the life of the process.
static void onLoaded(ModuleWatch* watch) {
  if (InterlockedExchange(&watch->state, 1) != 0) {
    return;
  }
  if (watch->isPinned) {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, watch->moduleName, &pinned)) {
      fatalError("Could not keep %s loaded (error %lu)", watch->moduleName, GetLastError());
    }
  }
  watch->onLoad();
}

static void onUnloaded(ModuleWatch* watch) {
  if (!watch->isPinned && InterlockedExchange(&watch->state, 0) == 1) {
    watch->onUnload();
  }
}

static VOID CALLBACK onDllNotification(ULONG reason, const DllNotificationData* data, PVOID context) {
  ModuleWatch* const watch = static_cast<ModuleWatch*>(context);
  if (data->baseDllName == nullptr || !isNamed(data->baseDllName, watch->moduleName)) {
    return;
  }
  if (reason == dllLoadedReason) {
    onLoaded(watch);
  } else if (reason == dllUnloadedReason) {
    onUnloaded(watch);
  }
}

static void watchModule(const char* moduleName, void (*onLoad)(), void (*onUnload)(), bool isPinned) {
  if (strlen(moduleName) >= moduleNameCapacity) {
    fatalError("The module name %s is longer than %zu characters", moduleName, moduleNameCapacity - 1);
  }
  const LONG slot = InterlockedIncrement(&moduleWatchCount) - 1;
  if (slot >= moduleWatchCapacity) {
    fatalError("More than %ld modules are watched; raise moduleWatchCapacity", moduleWatchCapacity);
  }
  ModuleWatch* const watch = &moduleWatches[slot];
  strcpy(watch->moduleName, moduleName);
  watch->onLoad = onLoad;
  watch->onUnload = onUnload;
  watch->isPinned = isPinned;
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  LdrRegisterDllNotificationFunction ldrRegisterDllNotification = ntdll == nullptr ? nullptr :
    reinterpret_cast<LdrRegisterDllNotificationFunction>(GetProcAddress(ntdll, "LdrRegisterDllNotification"));
  if (ldrRegisterDllNotification == nullptr) {
    fatalError("ntdll.dll does not export LdrRegisterDllNotification");
  }
  // Registered before the check for a module already loaded, so a load between the two is not missed.
  PVOID cookie = nullptr;
  const NTSTATUS status = ldrRegisterDllNotification(0, &onDllNotification, watch, &cookie);
  if (status < 0) {
    fatalError("LdrRegisterDllNotification failed (status 0x%08lX)", static_cast<unsigned long>(status));
  }
  if (GetModuleHandleA(moduleName) != nullptr) {
    onLoaded(watch);
  }
}

void whenModuleLoads(const char* moduleName, void (*callback)()) {
  watchModule(moduleName, callback, nullptr, true);
}

void watchModuleLoads(const char* moduleName, void (*onLoad)(), void (*onUnload)()) {
  watchModule(moduleName, onLoad, onUnload, false);
}
