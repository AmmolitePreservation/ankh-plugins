#pragma once

// Which handlerName.cpp the build compiled in: "default" for the plugin's own, a game's name for that game's.
const char* handlerName();

// The greeting in the handler's own settings file in pluginDirectory; "none" from a handler that has no such file.
const char* loadHandlerGreeting(const char* pluginDirectory);
