#include "configuration.h"

#include "fatalError.h"
#include "settingsFile.h"

Configuration loadConfiguration(const std::string& pluginDirectory) {
  Configuration configuration = {};
  std::string renderResolution;
  std::string windowModules;
  const SettingDefinition definitions[] = {
    { "monitor", SettingKind::text, &configuration.monitor, false },
    { "renderResolution", SettingKind::text, &renderResolution, false },
    { "startMinimized", SettingKind::boolean, &configuration.startMinimized, false },
    { "releaseInput", SettingKind::boolean, &configuration.releaseInput, false },
    { "confineCursor", SettingKind::boolean, &configuration.confineCursor, false },
    { "windowModules", SettingKind::anyText, &windowModules, false },
  };
  loadSettingsFile(pluginDirectory, "display-guard.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  if (renderResolution == "native") {
    configuration.renderResolution = RenderResolution::native;
  } else if (renderResolution == "game") {
    configuration.renderResolution = RenderResolution::game;
  } else {
    fatalError("display-guard.cfg: renderResolution must be native or game, not '%s'", renderResolution.c_str());
  }
  size_t start = 0;
  while (!windowModules.empty() && start <= windowModules.size()) {
    const size_t comma = windowModules.find(',', start);
    const std::string name = windowModules.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (name.empty() || name.find_first_of(" \t\\/") != std::string::npos) {
      fatalError("display-guard.cfg: windowModules must be library file names separated by commas, not '%s'",
        windowModules.c_str());
    }
    configuration.windowModules.push_back(name);
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return configuration;
}
