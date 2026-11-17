#pragma once

#include <string>

// Where the game's client connects for this launch: a host name, or host:port, that the launcher hands over in
// ankhPluginsServerAddress when its world names one. Empty when the game was started without it, and the client then goes
// where its own files point, as it always did.

// Reads the launcher's variable. Fatal when it is not a host name with an optional port.
void loadServerAddress();

// The whole address as the launcher gave it.
const std::string& serverAddress();

// Its host name, and its port, 0 when it names none.
const std::string& serverHostName();
unsigned short serverPort();
