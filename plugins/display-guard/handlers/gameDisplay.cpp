#include "gameDisplay.h"

void prepareGameDisplay(const PluginHost*, const RECT&) {
}

TakeoverGuard gameTakeoverGuard() {
  return TakeoverGuard::none;
}
