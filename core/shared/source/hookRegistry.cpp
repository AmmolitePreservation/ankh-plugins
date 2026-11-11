#include "hookRegistry.h"

#include "fatalError.h"
#include "sessionLog.h"

#include <MinHook.h>

#include <windows.h>

#include <cstdio>
#include <cstring>

static const size_t targetNameCapacity = 128;

// The name is copied: installExportHook builds it in a buffer that is gone when it returns.
struct HookedTarget {
  void* address;
  char name[targetNameCapacity];
};

static const size_t hookedTargetCapacity = 64;
static HookedTarget hookedTargets[hookedTargetCapacity];
static size_t hookedTargetCount = 0;
static bool isInitialized = false;
// Hooks are installed at load, as a module loads, and from inside hooked calls on the game's threads.
static SRWLOCK registryLock = SRWLOCK_INIT;

static const HookedTarget* findTarget(void* address) {
  for (size_t index = 0; index < hookedTargetCount; index++) {
    if (hookedTargets[index].address == address) {
      return &hookedTargets[index];
    }
  }
  return nullptr;
}

static void rememberTarget(void* address, const char* name) {
  if (hookedTargetCount == hookedTargetCapacity) {
    fatalError("Hook registry is full; raise hookedTargetCapacity");
  }
  if (strlen(name) >= targetNameCapacity) {
    fatalError("The hook name %s is longer than %zu characters", name, targetNameCapacity - 1);
  }
  hookedTargets[hookedTargetCount].address = address;
  strcpy(hookedTargets[hookedTargetCount].name, name);
  hookedTargetCount++;
}

static void requireMinHookSuccess(MH_STATUS status, const char* action, const char* targetName) {
  if (status != MH_OK) {
    fatalError("MinHook failed to %s %s: %s", action, targetName, MH_StatusToString(status));
  }
}

void initializeHookRegistry() {
  if (isInitialized) {
    fatalError("initializeHookRegistry called twice");
  }
  requireMinHookSuccess(MH_Initialize(), "initialize", "itself");
  isInitialized = true;
}

void* resolveLoadedExport(const char* moduleName, const char* exportName) {
  HMODULE module = GetModuleHandleA(moduleName);
  if (module == nullptr) {
    fatalError("%s is not loaded (error %lu)", moduleName, GetLastError());
  }
  void* address = reinterpret_cast<void*>(GetProcAddress(module, exportName));
  if (address == nullptr) {
    fatalError("%s does not export %s (error %lu)", moduleName, exportName, GetLastError());
  }
  return address;
}

bool installHook(const char* targetName, void* target, void* detour, void** original) {
  if (!isInitialized) {
    fatalError("installHook(%s) called before initializeHookRegistry", targetName);
  }
  AcquireSRWLockExclusive(&registryLock);
  const HookedTarget* const sharedWith = findTarget(target);
  if (sharedWith == nullptr) {
    requireMinHookSuccess(MH_CreateHook(target, detour, original), "create the hook for", targetName);
    requireMinHookSuccess(MH_EnableHook(target), "enable the hook for", targetName);
    rememberTarget(target, targetName);
  }
  ReleaseSRWLockExclusive(&registryLock);
  if (sharedWith != nullptr) {
    writeLogLine("INFO", "hookShared target=%s sharedWith=%s", targetName, sharedWith->name);
    return false;
  }
  writeLogLine("INFO", "hookInstalled target=%s", targetName);
  return true;
}

// Writes <prefix><module>!<export>, the module's name lacking ".dll".
static void nameExport(char* targetName, const char* prefix, const char* moduleName, const char* exportName) {
  const char* const dllSuffix = ".dll";
  const size_t moduleNameLength = strlen(moduleName);
  const size_t suffixLength = strlen(dllSuffix);
  const size_t baseLength = moduleNameLength > suffixLength && _stricmp(moduleName + moduleNameLength - suffixLength, dllSuffix) == 0
    ? moduleNameLength - suffixLength : moduleNameLength;
  const int written = snprintf(targetName, targetNameCapacity, "%s%.*s!%s", prefix, static_cast<int>(baseLength), moduleName,
    exportName);
  if (written < 0 || static_cast<size_t>(written) >= targetNameCapacity) {
    fatalError("The hook name for %s of %s is longer than %zu characters", exportName, moduleName, targetNameCapacity - 1);
  }
}

bool installExportHook(const char* moduleName, const char* exportName, void* detour, void** original) {
  char targetName[targetNameCapacity];
  nameExport(targetName, "", moduleName, exportName);
  return installHook(targetName, resolveLoadedExport(moduleName, exportName), detour, original);
}

void installHookOnce(const char* targetName, void* target, void* detour, void** original) {
  AcquireSRWLockShared(&registryLock);
  void* installedAt = nullptr;
  for (size_t index = 0; index < hookedTargetCount; index++) {
    if (strcmp(hookedTargets[index].name, targetName) == 0) {
      installedAt = hookedTargets[index].address;
    }
  }
  ReleaseSRWLockShared(&registryLock);
  if (installedAt == nullptr) {
    installHook(targetName, target, detour, original);
  } else if (installedAt != target) {
    fatalError("%s is hooked at %p, but the game now also uses the one at %p", targetName, installedAt, target);
  }
}

static void registerTarget(void* address, const char* name) {
  AcquireSRWLockExclusive(&registryLock);
  const HookedTarget* const changedBy = findTarget(address);
  if (changedBy != nullptr) {
    fatalError("%s patches code that %s already changed", name, changedBy->name);
  }
  rememberTarget(address, name);
  ReleaseSRWLockExclusive(&registryLock);
}

template <typename Type>
static Type* atOffset(HMODULE module, size_t offset) {
  return reinterpret_cast<Type*>(reinterpret_cast<unsigned char*>(module) + offset);
}

static const IMAGE_DATA_DIRECTORY& dataDirectory(HMODULE module, size_t entry) {
  const IMAGE_NT_HEADERS* const headers = atOffset<const IMAGE_NT_HEADERS>(module,
    atOffset<const IMAGE_DOS_HEADER>(module, 0)->e_lfanew);
  return headers->OptionalHeader.DataDirectory[entry];
}

// A delay-loaded import is bound at its first call, through a table this hook does not patch.
static void requireNoDelayLoad(HMODULE importingModule, const char* importedModuleName) {
  const IMAGE_DATA_DIRECTORY& directory = dataDirectory(importingModule, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
  if (directory.VirtualAddress == 0) {
    return;
  }
  for (const IMAGE_DELAYLOAD_DESCRIPTOR* descriptor = atOffset<const IMAGE_DELAYLOAD_DESCRIPTOR>(importingModule,
      directory.VirtualAddress); descriptor->DllNameRVA != 0; descriptor++) {
    // Linkers older than Visual C++ 7 wrote addresses here instead of offsets from the module's base.
    const char* const delayedModuleName = descriptor->Attributes.RvaBased
      ? atOffset<const char>(importingModule, descriptor->DllNameRVA)
      : reinterpret_cast<const char*>(static_cast<uintptr_t>(descriptor->DllNameRVA));
    if (_stricmp(delayedModuleName, importedModuleName) == 0) {
      fatalError("The game delay-loads %s, whose imports cannot be hooked", importedModuleName);
    }
  }
}

static void** findImportSlot(HMODULE importingModule, const char* importedModuleName, const char* functionName) {
  const IMAGE_DATA_DIRECTORY& directory = dataDirectory(importingModule, IMAGE_DIRECTORY_ENTRY_IMPORT);
  if (directory.VirtualAddress == 0) {
    return nullptr;
  }
  void** slot = nullptr;
  for (const IMAGE_IMPORT_DESCRIPTOR* descriptor = atOffset<const IMAGE_IMPORT_DESCRIPTOR>(importingModule,
      directory.VirtualAddress); descriptor->Name != 0; descriptor++) {
    if (_stricmp(atOffset<const char>(importingModule, descriptor->Name), importedModuleName) != 0) {
      continue;
    }
    // The slots themselves hold addresses, which another plugin may already have replaced; only this table of
    // names says which function a slot is for.
    if (descriptor->OriginalFirstThunk == 0) {
      fatalError("The game's imports from %s carry no names, so they cannot be hooked", importedModuleName);
    }
    const IMAGE_THUNK_DATA* name = atOffset<const IMAGE_THUNK_DATA>(importingModule, descriptor->OriginalFirstThunk);
    IMAGE_THUNK_DATA* address = atOffset<IMAGE_THUNK_DATA>(importingModule, descriptor->FirstThunk);
    for (; name->u1.AddressOfData != 0; name++, address++) {
      if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)) {
        fatalError("The game imports from %s by ordinal, which cannot be hooked", importedModuleName);
      }
      if (strcmp(atOffset<const IMAGE_IMPORT_BY_NAME>(importingModule, name->u1.AddressOfData)->Name, functionName) != 0) {
        continue;
      }
      if (slot != nullptr) {
        fatalError("The game imports %s from %s twice", functionName, importedModuleName);
      }
      slot = reinterpret_cast<void**>(&address->u1.Function);
    }
  }
  return slot;
}

bool installImportHook(HMODULE importingModule, const char* importedModuleName, const char* functionName, void* detour,
    void** original) {
  char targetName[targetNameCapacity];
  nameExport(targetName, "import of ", importedModuleName, functionName);
  requireNoDelayLoad(importingModule, importedModuleName);
  void** const slot = findImportSlot(importingModule, importedModuleName, functionName);
  if (slot == nullptr) {
    writeLogLine("INFO", "importAbsent target=%s", targetName);
    return false;
  }
  registerTarget(slot, targetName);
  DWORD previousProtection = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previousProtection)) {
    fatalError("%s: could not unprotect the import table (error %lu)", targetName, GetLastError());
  }
  *original = *slot;
  *slot = detour;
  DWORD ignoredProtection = 0;
  if (!VirtualProtect(slot, sizeof(*slot), previousProtection, &ignoredProtection)) {
    fatalError("%s: could not reprotect the import table (error %lu)", targetName, GetLastError());
  }
  writeLogLine("INFO", "importHooked target=%s", targetName);
  return true;
}

void patchCode(const char* patchName, void* address, const unsigned char* expected, const unsigned char* replacement, size_t size) {
  if (memcmp(address, expected, size) != 0) {
    fatalError("%s: the game's code at %p is not what the patch expects", patchName, address);
  }
  registerTarget(address, patchName);
  DWORD previousProtection = 0;
  if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previousProtection)) {
    fatalError("%s: could not unprotect the game's code (error %lu)", patchName, GetLastError());
  }
  memcpy(address, replacement, size);
  DWORD ignoredProtection = 0;
  if (!VirtualProtect(address, size, previousProtection, &ignoredProtection)) {
    fatalError("%s: could not reprotect the game's code (error %lu)", patchName, GetLastError());
  }
  if (!FlushInstructionCache(GetCurrentProcess(), address, size)) {
    fatalError("%s: could not flush the instruction cache (error %lu)", patchName, GetLastError());
  }
  writeLogLine("INFO", "codePatched patch=%s address=%p", patchName, address);
}

void redirectCall(const char* patchName, void* callSite, void* expectedTarget, void* newTarget) {
  const unsigned char* const site = static_cast<const unsigned char*>(callSite);
  const auto relativeTo = [site](void* target) {
    return static_cast<long>(reinterpret_cast<const unsigned char*>(target) - (site + 5));
  };
  unsigned char expected[5] = { 0xE8 };
  unsigned char replacement[5] = { 0xE8 };
  const long expectedOffset = relativeTo(expectedTarget);
  const long newOffset = relativeTo(newTarget);
  memcpy(expected + 1, &expectedOffset, 4);
  memcpy(replacement + 1, &newOffset, 4);
  patchCode(patchName, callSite, expected, replacement, sizeof(expected));
}
