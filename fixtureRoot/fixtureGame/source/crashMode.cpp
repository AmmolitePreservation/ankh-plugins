#include "crashMode.h"

#include "fixtureReport.h"

#include <windows.h>

static LONG WINAPI fixtureFilter(EXCEPTION_POINTERS* exception) {
  writeReport("gameFilterRan code=0x%08lX", exception->ExceptionRecord->ExceptionCode);
  return EXCEPTION_EXECUTE_HANDLER;
}

int runCrashMode() {
  if (!openReport()) {
    return 8;
  }
  SetUnhandledExceptionFilter(&fixtureFilter);
  writeReport("crashing");
  *static_cast<volatile int*>(nullptr) = 1;
  failStep("crash", 0);
  return 9;
}
