#include "configuration.h"

#include "fatalError.h"
#include "settingsFile.h"

static const unsigned long trimSecondsLimit = 86400;

Configuration loadConfiguration(const std::string& pluginDirectory) {
  Configuration configuration = {};
  const SettingDefinition definitions[] = {
    { "backgroundTrimSeconds", SettingKind::wholeNumber, &configuration.backgroundTrimSeconds, false },
    { "foregroundTrimSeconds", SettingKind::wholeNumber, &configuration.foregroundTrimSeconds, false },
  };
  loadSettingsFile(pluginDirectory, "memory-trim.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  if (configuration.backgroundTrimSeconds > trimSecondsLimit) {
    fatalError("memory-trim.cfg: backgroundTrimSeconds must be at most %lu", trimSecondsLimit);
  }
  if (configuration.foregroundTrimSeconds > trimSecondsLimit) {
    fatalError("memory-trim.cfg: foregroundTrimSeconds must be at most %lu", trimSecondsLimit);
  }
  return configuration;
}
