#include "configuration.h"

#include "fatalError.h"
#include "settingsFile.h"

Configuration loadConfiguration(const std::string& pluginDirectory) {
  Configuration configuration = {};
  std::string renderResolution;
  const SettingDefinition definitions[] = {
    { "monitor", SettingKind::text, &configuration.monitor, false },
    { "renderResolution", SettingKind::text, &renderResolution, false },
    { "startMinimized", SettingKind::boolean, &configuration.startMinimized, false },
    { "releaseInput", SettingKind::boolean, &configuration.releaseInput, false },
  };
  loadSettingsFile(pluginDirectory, "display-guard.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  if (renderResolution == "native") {
    configuration.renderResolution = RenderResolution::native;
  } else if (renderResolution == "game") {
    configuration.renderResolution = RenderResolution::game;
  } else {
    fatalError("display-guard.cfg: renderResolution must be native or game, not '%s'", renderResolution.c_str());
  }
  return configuration;
}
