#include "workingSetTrim.h"

#include "fatalError.h"
#include "gameForeground.h"
#include "sessionLog.h"

#include <windows.h>
#include <psapi.h>

static unsigned long backgroundTrimSeconds = 0;
static unsigned long foregroundTrimSeconds = 0;
static ULONGLONG lastTrimAt = 0;

static unsigned long long workingSetKilobytes() {
  PROCESS_MEMORY_COUNTERS counters = {};
  counters.cb = sizeof(counters);
  if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    fatalError("The game's memory use could not be read (%lu)", GetLastError());
  }
  return counters.WorkingSetSize / 1024;
}

// Removing every page from the working set leaves them in RAM on Windows' standby list, so a page the game touches
// again comes back without reading the disk; only when RAM runs short are the untouched ones written out.
static void trimWhenDue() {
  const bool foreground = isGameInForeground();
  const unsigned long seconds = foreground ? foregroundTrimSeconds : backgroundTrimSeconds;
  if (seconds == 0) {
    return;
  }
  const ULONGLONG now = GetTickCount64();
  if (now - lastTrimAt < seconds * 1000ULL) {
    return;
  }
  lastTrimAt = now;
  const unsigned long long before = workingSetKilobytes();
  if (!SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0)) {
    fatalError("The game's working set could not be trimmed (%lu)", GetLastError());
  }
  writeLogLine("INFO", "workingSetTrimmed foreground=%d beforeKB=%llu afterKB=%llu", foreground ? 1 : 0, before,
    workingSetKilobytes());
}

void installWorkingSetTrim(const Configuration& configuration, const PluginHost* host) {
  backgroundTrimSeconds = configuration.backgroundTrimSeconds;
  foregroundTrimSeconds = configuration.foregroundTrimSeconds;
  if (backgroundTrimSeconds == 0 && foregroundTrimSeconds == 0) {
    writeLogLine("INFO", "memoryTrimOff");
    return;
  }
  lastTrimAt = GetTickCount64();
  host->addPumpCallback(&trimWhenDue);
  writeLogLine("INFO", "memoryTrimEnabled backgroundTrimSeconds=%lu foregroundTrimSeconds=%lu", backgroundTrimSeconds,
    foregroundTrimSeconds);
}
