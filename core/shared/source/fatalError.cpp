#include "fatalError.h"

#include "sessionLog.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

static const char* fatalErrorTitle = "diagnostic";

void setFatalErrorTitle(const char* pluginName) {
  fatalErrorTitle = pluginName;
}

[[noreturn]] void fatalError(const char* format, ...) {
  char message[1024];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  message[sizeof(message) - 1] = '\0';
  // The process is about to die, so a failed log write has nowhere to go.
  tryWriteLogLine("ERROR", message);

  MessageBoxA(nullptr, message, fatalErrorTitle, MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
  TerminateProcess(GetCurrentProcess(), 1);
  for (;;) {
    Sleep(INFINITE);
  }
}
