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
static const size_t moduleNameCapacity = 64;
static const LONG moduleWatchCapacity = 8;

struct ModuleWatch {
  char moduleName[moduleNameCapacity];
  void (*callback)();
  volatile LONG hasRun;
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

static void runOnce(ModuleWatch* watch) {
  if (InterlockedExchange(&watch->hasRun, 1) == 0) {
    watch->callback();
  }
}

static VOID CALLBACK onDllNotification(ULONG reason, const DllNotificationData* data, PVOID context) {
  ModuleWatch* const watch = static_cast<ModuleWatch*>(context);
  if (reason == dllLoadedReason && data->baseDllName != nullptr && isNamed(data->baseDllName, watch->moduleName)) {
    runOnce(watch);
  }
}

void whenModuleLoads(const char* moduleName, void (*callback)()) {
  if (strlen(moduleName) >= moduleNameCapacity) {
    fatalError("The module name %s is longer than %zu characters", moduleName, moduleNameCapacity - 1);
  }
  const LONG slot = InterlockedIncrement(&moduleWatchCount) - 1;
  if (slot >= moduleWatchCapacity) {
    fatalError("More than %ld modules are watched; raise moduleWatchCapacity", moduleWatchCapacity);
  }
  ModuleWatch* const watch = &moduleWatches[slot];
  strcpy(watch->moduleName, moduleName);
  watch->callback = callback;
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
    runOnce(watch);
  }
}
