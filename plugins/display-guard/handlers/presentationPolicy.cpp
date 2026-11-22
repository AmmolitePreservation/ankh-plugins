#include "presentationPolicy.h"

#include "windowPlacement.h"

void initializePresentationPolicy(const PluginHost*, RenderResolution) {
}

RECT presentationRectangle(UINT backBufferWidth, UINT backBufferHeight, const RECT& target) {
  return letterboxRectangle(backBufferWidth, backBufferHeight, target);
}

SIZE nativeBackBufferSize(LONG windowWidth, LONG windowHeight) {
  return { windowWidth, windowHeight };
}
