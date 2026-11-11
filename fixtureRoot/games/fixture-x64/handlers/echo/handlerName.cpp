#include "handlerName.h"

#include "source/handlerGreeting.h"

const char* handlerName() {
  return "fixture-x64";
}

const char* loadHandlerGreeting(const char* pluginDirectory) {
  return readHandlerGreeting(pluginDirectory);
}
