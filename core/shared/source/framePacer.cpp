#include "framePacer.h"

#include "fatalError.h"
#include "gameForeground.h"

#include <windows.h>

static unsigned long foregroundRate = 0;
static unsigned long backgroundRate = 0;
static HANDLE timer = nullptr;
static LONGLONG countsPerSecond = 0;
static LONGLONG previousPresent = 0;
static unsigned long previousFramesPerSecond = 0;

static LONGLONG counterNow() {
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  return counter.QuadPart;
}

void startFramePacer(unsigned long foregroundFramesPerSecond, unsigned long backgroundFramesPerSecond) {
  if (timer != nullptr) {
    fatalError("startFramePacer called twice");
  }
  foregroundRate = foregroundFramesPerSecond;
  backgroundRate = backgroundFramesPerSecond;
  LARGE_INTEGER frequency = {};
  if (!QueryPerformanceFrequency(&frequency)) {
    fatalError("QueryPerformanceFrequency failed (%lu)", GetLastError());
  }
  countsPerSecond = frequency.QuadPart;
  timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (timer == nullptr) {
    fatalError("A high-resolution timer could not be created (%lu)", GetLastError());
  }
}

// A frame is due one interval after the one before it. The interval starts over when the rate changes, so a
// game coming back to the foreground is not held to its background rate for one more frame.
void paceFrame() {
  const unsigned long framesPerSecond = isGameInForeground() ? foregroundRate : backgroundRate;
  if (framesPerSecond == 0) {
    previousFramesPerSecond = 0;
    return;
  }
  LONGLONG presentedAt = counterNow();
  if (framesPerSecond == previousFramesPerSecond) {
    const LONGLONG remaining = previousPresent + countsPerSecond / framesPerSecond - presentedAt;
    if (remaining > 0) {
      LARGE_INTEGER due = {};
      due.QuadPart = -((remaining * 10000000 + countsPerSecond - 1) / countsPerSecond);
      if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
        fatalError("Frame pacing timer failed (%lu)", GetLastError());
      }
      presentedAt = counterNow();
    }
  }
  previousPresent = presentedAt;
  previousFramesPerSecond = framesPerSecond;
}
