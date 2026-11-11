#include "presentationPolicy.h"

#include "windowPlacement.h"

void initializePresentationPolicy(const PluginHost*) {
}

RECT presentationRectangle(UINT backBufferWidth, UINT backBufferHeight, const RECT& target) {
  return letterboxRectangle(backBufferWidth, backBufferHeight, target);
}
