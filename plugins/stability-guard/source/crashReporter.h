#pragma once

#include "configuration.h"

#include <windows.h>

// Reports crashes into the session log, and with writeMemoryDumps also as memory dumps (startMemoryDumps
// first). A first-chance handler reports each serious exception before the game's own handlers see it,
// up to maxFullReports distinct addresses, with a dump of the stacks; the game may still recover from
// these. An exception nothing handles is fatal: it is reported, and the process's first gets a dump of
// all its memory, whatever the report count, before the game's own unhandled-exception filter runs.
// Neither ever swallows an exception.
void installCrashReporter(const Configuration& configuration, DWORD mainThreadID);

// A fault at address is the game's own: it faults there on purpose and catches the fault itself, even when its catch
// asks the unhandled-exception filter, so it is counted and never reported or dumped. reason names it in the log the
// first time. Up to 16 addresses; a game handler calls this while the plugin loads.
void expectFault(uintptr_t address, const char* reason);

// Writes the session's crash counts. Called once, at process detach.
void writeCrashTotals();
