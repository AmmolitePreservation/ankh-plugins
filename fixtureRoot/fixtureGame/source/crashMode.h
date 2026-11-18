#pragma once

// The crash mode: installs an unhandled-exception filter of its own, as a game with a crash dialog does, writes
// "crashing" and faults; the filter writes "gameFilterRan" and lets the process end. Never returns unless the fault
// is somehow survived; then it returns main.cpp's exit code for a failed step.
int runCrashMode();
