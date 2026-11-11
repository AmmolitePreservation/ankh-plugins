#include "configuration.h"

#include "fatalError.h"
#include "settingsFile.h"

static const unsigned long framesPerSecondLimit = 1000;

Configuration loadConfiguration(const std::string& pluginDirectory) {
  Configuration configuration = {};
  const SettingDefinition definitions[] = {
    { "foregroundFramesPerSecond", SettingKind::wholeNumber, &configuration.foregroundFramesPerSecond, false },
    { "backgroundFramesPerSecond", SettingKind::wholeNumber, &configuration.backgroundFramesPerSecond, false },
  };
  loadSettingsFile(pluginDirectory, "frame-control.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  if (configuration.foregroundFramesPerSecond > framesPerSecondLimit) {
    fatalError("frame-control.cfg: foregroundFramesPerSecond must be at most %lu", framesPerSecondLimit);
  }
  if (configuration.backgroundFramesPerSecond > framesPerSecondLimit) {
    fatalError("frame-control.cfg: backgroundFramesPerSecond must be at most %lu", framesPerSecondLimit);
  }
  return configuration;
}
