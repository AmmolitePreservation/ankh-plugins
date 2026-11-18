#include "gameFixes.h"

#include "crashReporter.h"

// The x64 fixture stands in for a game whose crash filter sends its dumps away: its fatal crashes never reach the
// game's filter. The x86 fixture keeps the central handler, so the tests see both.
void installGameFixes(const std::string&, const Configuration&) {
  withholdFatalCrashesFromGameFilter("fixtureWithholds");
}
