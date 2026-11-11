#pragma once

// Loads the system copy of the DLL this loader stands in for and points every proxy export at it.
// Called from DllMain, before the game's own code can call any of them.
void loadSystemProxyLibrary();
