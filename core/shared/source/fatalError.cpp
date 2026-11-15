#include "fatalError.h"

#include "sessionLog.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

static const char* fatalErrorTitle = "diagnostic";
static const DWORD boxStartMilliseconds = 3000;
// The thread whose fatal error is being shown, 0 before any.
static volatile LONG fatalThreadID = 0;
static char fatalMessage[1024];
static HANDLE boxStarted = nullptr;

void setFatalErrorTitle(const char* pluginName) {
  fatalErrorTitle = pluginName;
}

static DWORD WINAPI showFatalMessage(LPVOID) {
  SetEvent(boxStarted);
  MessageBoxA(nullptr, fatalMessage, fatalErrorTitle, MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
  TerminateProcess(GetCurrentProcess(), 1);
  return 0;
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

  // One box, the first error's. A message box runs a message loop that hands the game's window its messages, so a box
  // on the failing thread can run the code that failed again, a box at a time: it is shown from a thread of its own
  // while the failing thread waits. A thread that cannot start (the failing thread holds the loader lock, or the process
  // is already ending) leaves the box to the failing thread, and an error there from inside its loop ends the process.
  const DWORD threadID = GetCurrentThreadId();
  const LONG shownBy = InterlockedCompareExchange(&fatalThreadID, static_cast<LONG>(threadID), 0);
  if (shownBy == static_cast<LONG>(threadID)) {
    TerminateProcess(GetCurrentProcess(), 1);
  }
  if (shownBy == 0) {
    strcpy(fatalMessage, message);
    boxStarted = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    const HANDLE boxThread = boxStarted == nullptr ? nullptr : CreateThread(nullptr, 0, &showFatalMessage, nullptr, 0, nullptr);
    if (boxThread == nullptr || WaitForSingleObject(boxStarted, boxStartMilliseconds) != WAIT_OBJECT_0) {
      MessageBoxA(nullptr, message, fatalErrorTitle, MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
      TerminateProcess(GetCurrentProcess(), 1);
    }
  }
  for (;;) {
    Sleep(INFINITE);
  }
}
