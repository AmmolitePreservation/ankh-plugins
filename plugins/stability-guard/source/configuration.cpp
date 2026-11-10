#include "configuration.h"

#include "fatalError.h"
#include "settingsFile.h"

static const unsigned long long bytesPerGigabyte = 1024ULL * 1024ULL * 1024ULL;

Configuration loadConfiguration(const std::string& pluginDirectory) {
  Configuration configuration = {};
  unsigned long maxDumpGigabytes = 0;
  const SettingDefinition definitions[] = {
    { "maxFullReports", SettingKind::positiveInteger, &configuration.maxFullReports, false },
    { "writeMemoryDumps", SettingKind::boolean, &configuration.writeMemoryDumps, false },
    { "maxDumpGigabytes", SettingKind::wholeNumber, &maxDumpGigabytes, false },
    { "stackScanSlots", SettingKind::positiveInteger, &configuration.stackScanSlots, false },
    { "freezeSeconds", SettingKind::wholeNumber, &configuration.freezeSeconds, false },
    { "exitGuard", SettingKind::boolean, &configuration.exitGuard, false },
    { "exitGraceSeconds", SettingKind::positiveInteger, &configuration.exitGraceSeconds, false },
    { "forceExitHotkey", SettingKind::boolean, &configuration.forceExitHotkey, false },
  };
  loadSettingsFile(pluginDirectory, "stability-guard.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  configuration.maxDumpBytes = maxDumpGigabytes * bytesPerGigabyte;
  if (configuration.exitGraceSeconds > 600) {
    fatalError("stability-guard.cfg: exitGraceSeconds must be at most 600");
  }
  if (configuration.forceExitHotkey && !configuration.exitGuard) {
    fatalError("stability-guard.cfg: forceExitHotkey=true needs exitGuard=true");
  }
  if (configuration.stackScanSlots > stackScanSlotLimit) {
    fatalError("stability-guard.cfg: stackScanSlots must be at most %lu", stackScanSlotLimit);
  }
  return configuration;
}
