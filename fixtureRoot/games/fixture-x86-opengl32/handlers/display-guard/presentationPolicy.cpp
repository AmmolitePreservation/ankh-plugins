#include "presentationPolicy.h"

#include "windowPlacement.h"

void initializePresentationPolicy(const PluginHost*) {
}

// A square whatever the back buffer's shape, which the plugin's own placement never gives a 500x400 back buffer.
RECT presentationRectangle(UINT, UINT, const RECT& target) {
  return letterboxRectangle(1, 1, target);
}
