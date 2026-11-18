#pragma once

#include "configuration.h"

// An OpenGL game draws into its window itself: the window it sets a pixel format on becomes the borderless game window,
// and the game renders at the size of the monitor it covers, so renderResolution must be native.
void installOpenGLHooks(const Configuration& configuration);
