#include "gameForeground.h"

#include <windows.h>

bool isGameInForeground() {
  DWORD foregroundProcess = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
  return foregroundProcess == GetCurrentProcessId();
}
