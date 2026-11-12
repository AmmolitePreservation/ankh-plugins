#include "handlerGreeting.h"

#include "settingsFile.h"

#include <string>

const char* readHandlerGreeting(const char* pluginDirectory) {
  static std::string greeting;
  const SettingDefinition definitions[] = { { "greeting", SettingKind::text, &greeting, false } };
  loadSettingsFile(pluginDirectory, "echo.handler.cfg", definitions, 1);
  return greeting.c_str();
}
