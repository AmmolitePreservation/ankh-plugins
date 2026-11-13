#include "daoRedirect.h"

#include "fatalError.h"
#include "hookRegistry.h"
#include "sessionLog.h"

#include <objbase.h>

namespace {

// DAO's class identifiers differ only in their first part: DAO 3.5's run from 0x10 (DBEngine) to 0x19 (Relation),
// DAO 3.6's from 0x100 to 0x109, in the same order.
const GUID daoClassTail = { 0, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4 } };
const unsigned long firstDAO35Class = 0x10;
const unsigned long lastDAO35Class = 0x19;
const unsigned long firstDAO36Class = 0x100;

typedef HRESULT(WINAPI* CoGetClassObjectFunction)(REFCLSID classID, DWORD context, LPVOID serverInfo, REFIID interfaceID,
  LPVOID* object);
typedef HRESULT(WINAPI* CoCreateInstanceFunction)(REFCLSID classID, LPUNKNOWN outer, DWORD context, REFIID interfaceID,
  LPVOID* object);
typedef HRESULT(WINAPI* CoCreateInstanceExFunction)(REFCLSID classID, IUnknown* outer, DWORD context,
  COSERVERINFO* serverInfo, DWORD resultCount, MULTI_QI* results);

CoGetClassObjectFunction originalCoGetClassObject = nullptr;
CoCreateInstanceFunction originalCoCreateInstance = nullptr;
CoCreateInstanceExFunction originalCoCreateInstanceEx = nullptr;

bool isDAO35Class(REFCLSID classID) {
  GUID tail = classID;
  tail.Data1 = 0;
  return IsEqualGUID(tail, daoClassTail) && classID.Data1 >= firstDAO35Class && classID.Data1 <= lastDAO35Class;
}

CLSID dao36ClassFor(REFCLSID classID) {
  CLSID replacement = classID;
  replacement.Data1 = classID.Data1 - firstDAO35Class + firstDAO36Class;
  return replacement;
}

// What COM answered for a redirected class: the game hears it as it is, except that DAO 3.6 missing from Windows is
// fatal, since the game would only report that it cannot open its database.
HRESULT checked(const char* call, REFCLSID requested, REFCLSID redirected, HRESULT result) {
  if (result == REGDB_E_CLASSNOTREG || result == CO_E_DLLNOTFOUND) {
    fatalError("The game uses DAO 3.5, which Windows lacks, and DAO 3.6 (Microsoft Jet 4.0, 32-bit) is not installed "
      "either: %s of class %08lX failed with 0x%08lX", call, redirected.Data1, static_cast<unsigned long>(result));
  }
  writeLogLine(SUCCEEDED(result) ? "INFO" : "WARN", "daoClassRedirected call=%s from=%08lX to=%08lX result=0x%08lX", call,
    requested.Data1, redirected.Data1, static_cast<unsigned long>(result));
  return result;
}

HRESULT WINAPI coGetClassObjectDetour(REFCLSID classID, DWORD context, LPVOID serverInfo, REFIID interfaceID,
    LPVOID* object) {
  if (!isDAO35Class(classID)) {
    return originalCoGetClassObject(classID, context, serverInfo, interfaceID, object);
  }
  const CLSID redirected = dao36ClassFor(classID);
  return checked("CoGetClassObject", classID, redirected,
    originalCoGetClassObject(redirected, context, serverInfo, interfaceID, object));
}

HRESULT WINAPI coCreateInstanceDetour(REFCLSID classID, LPUNKNOWN outer, DWORD context, REFIID interfaceID,
    LPVOID* object) {
  if (!isDAO35Class(classID)) {
    return originalCoCreateInstance(classID, outer, context, interfaceID, object);
  }
  const CLSID redirected = dao36ClassFor(classID);
  return checked("CoCreateInstance", classID, redirected,
    originalCoCreateInstance(redirected, outer, context, interfaceID, object));
}

HRESULT WINAPI coCreateInstanceExDetour(REFCLSID classID, IUnknown* outer, DWORD context, COSERVERINFO* serverInfo,
    DWORD resultCount, MULTI_QI* results) {
  if (!isDAO35Class(classID)) {
    return originalCoCreateInstanceEx(classID, outer, context, serverInfo, resultCount, results);
  }
  const CLSID redirected = dao36ClassFor(classID);
  return checked("CoCreateInstanceEx", classID, redirected,
    originalCoCreateInstanceEx(redirected, outer, context, serverInfo, resultCount, results));
}

}

void installDAORedirect() {
  const HMODULE game = GetModuleHandleW(nullptr);
  const bool isGetClassObjectHooked = installImportHook(game, "ole32.dll", "CoGetClassObject",
    reinterpret_cast<void*>(&coGetClassObjectDetour), reinterpret_cast<void**>(&originalCoGetClassObject));
  const bool isCreateInstanceHooked = installImportHook(game, "ole32.dll", "CoCreateInstance",
    reinterpret_cast<void*>(&coCreateInstanceDetour), reinterpret_cast<void**>(&originalCoCreateInstance));
  const bool isCreateInstanceExHooked = installImportHook(game, "ole32.dll", "CoCreateInstanceEx",
    reinterpret_cast<void*>(&coCreateInstanceExDetour), reinterpret_cast<void**>(&originalCoCreateInstanceEx));
  if (!isGetClassObjectHooked && !isCreateInstanceHooked && !isCreateInstanceExHooked) {
    fatalError("The game imports none of CoGetClassObject, CoCreateInstance and CoCreateInstanceEx from ole32.dll, so it "
      "makes no DAO object this plugin could redirect");
  }
  writeLogLine("INFO", "daoRedirectInstalled CoGetClassObject=%s CoCreateInstance=%s CoCreateInstanceEx=%s",
    isGetClassObjectHooked ? "hooked" : "notImported", isCreateInstanceHooked ? "hooked" : "notImported",
    isCreateInstanceExHooked ? "hooked" : "notImported");
}
