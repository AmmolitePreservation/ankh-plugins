#include "configuration.h"

#include "fatalError.h"
#include "framePacer.h"
#include "settingsFile.h"

#include <windows.h>

#include <cerrno>
#include <cstdlib>

namespace {

unsigned long primaryRefreshRate() {
  DEVMODEW mode = {};
  mode.dmSize = sizeof(mode);
  if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1) {
    fatalError("frame-control.cfg asks for auto, but the primary monitor reports no refresh rate");
  }
  return mode.dmDisplayFrequency;
}

// A number of frames a second, or auto for the primary monitor's refresh rate.
unsigned long framesPerSecond(const char* name, const std::string& text) {
  unsigned long rate = 0;
  if (text == "auto") {
    rate = primaryRefreshRate();
  } else {
    char* end = nullptr;
    errno = 0;
    rate = std::strtoul(text.c_str(), &end, 10);
    if (text.empty() || *end != '\0' || errno != 0 || text[0] == '-' || text[0] == '+') {
      fatalError("frame-control.cfg: %s must be auto or a whole number, not '%s'", name, text.c_str());
    }
  }
  if (rate > framesPerSecondLimit) {
    fatalError("frame-control.cfg: %s must be at most %lu", name, framesPerSecondLimit);
  }
  return rate;
}

}

Configuration loadConfiguration(const std::string& pluginDirectory) {
  std::string foreground;
  std::string background;
  const SettingDefinition definitions[] = {
    { "foregroundFramesPerSecond", SettingKind::text, &foreground, false },
    { "backgroundFramesPerSecond", SettingKind::text, &background, false },
  };
  loadSettingsFile(pluginDirectory, "frame-control.cfg", definitions, sizeof(definitions) / sizeof(definitions[0]));
  Configuration configuration = {};
  configuration.foregroundFramesPerSecond = framesPerSecond("foregroundFramesPerSecond", foreground);
  configuration.backgroundFramesPerSecond = framesPerSecond("backgroundFramesPerSecond", background);
  return configuration;
}
