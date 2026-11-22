#include "presentationPolicy.h"

#include "fatalError.h"
#include "windowPlacement.h"

static const volatile LONG* loginScreensShown = nullptr;

void initializePresentationPolicy(const PluginHost*, RenderResolution) {
  loginScreensShown = reinterpret_cast<const volatile LONG*>(GetProcAddress(GetModuleHandleA(nullptr), "fixtureLoginScreensShown"));
  if (loginScreensShown == nullptr) {
    fatalError("The fixture game exports no fixtureLoginScreensShown");
  }
}

// A square whatever the back buffer's shape, which the plugin's own placement never gives a 500x400 back buffer.
RECT presentationRectangle(UINT, UINT, const RECT& target) {
  return letterboxRectangle(1, 1, target);
}

// The largest square the window holds while the login screens are shown, as a game whose login screens have a shape of
// their own asks for; the window's size after.
SIZE nativeBackBufferSize(LONG windowWidth, LONG windowHeight) {
  if (*loginScreensShown == 0) {
    return { windowWidth, windowHeight };
  }
  const LONG side = windowWidth < windowHeight ? windowWidth : windowHeight;
  return { side, side };
}
