#pragma once

// Holds a game's frames to a rate while one of its windows is the foreground window and to another while it is not; 0
// leaves that case unlimited. Call startFramePacer once, then paceFrame from the game's present, before presenting.
// The highest rate a setting may ask for.
const unsigned long framesPerSecondLimit = 1000;

void startFramePacer(unsigned long foregroundFramesPerSecond, unsigned long backgroundFramesPerSecond);

void paceFrame();
