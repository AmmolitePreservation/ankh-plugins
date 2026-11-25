#include "configuration.h"

#include "fatalError.h"
#include "settingsFile.h"

Configuration loadConfiguration(const std::string& pluginDirectory) {
  Configuration configuration = {};
  std::string renderResolution;
  std::string takeoverGuard;
  const SettingDefinition definitions[] = {
    { "monitor", SettingKind::text, &configuration.monitor, false },
    { "renderResolution", SettingKind::text, &renderResolution, false },
    { "startMinimized", SettingKind::boolean, &configuration.startMinimized, false },
    { "releaseInput", SettingKind::boolean, &configuration.releaseInput, false },
    { "confineCursor", SettingKind::boolean, &configuration.confineCursor, false },
    { "takeoverGuard", SettingKind::text, &takeoverGuard, false },
  };
  loadSettingsFile(pluginDirectory, "display-guard.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  if (renderResolution == "native") {
    configuration.renderResolution = RenderResolution::native;
  } else if (renderResolution == "game") {
    configuration.renderResolution = RenderResolution::game;
  } else {
    fatalError("display-guard.cfg: renderResolution must be native or game, not '%s'", renderResolution.c_str());
  }
  if (takeoverGuard == "game") {
    configuration.takeoverGuard = gameTakeoverGuard();
  } else if (takeoverGuard == "none") {
    configuration.takeoverGuard = TakeoverGuard::none;
  } else if (takeoverGuard == "rowAbove") {
    configuration.takeoverGuard = TakeoverGuard::rowAbove;
  } else if (takeoverGuard == "layered") {
    configuration.takeoverGuard = TakeoverGuard::layered;
  } else {
    fatalError("display-guard.cfg: takeoverGuard must be game, none, rowAbove or layered, not '%s'", takeoverGuard.c_str());
  }
  return configuration;
}

const char* takeoverGuardName(TakeoverGuard guard) {
  switch (guard) {
    case TakeoverGuard::none:
      return "none";
    case TakeoverGuard::rowAbove:
      return "rowAbove";
    case TakeoverGuard::layered:
      return "layered";
  }
  fatalError("Unknown takeover guard %d", static_cast<int>(guard));
}
