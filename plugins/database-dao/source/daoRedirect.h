#pragma once

// A game built with DAO 3.5 asks COM for its classes (DBEngine and the rest) by their class identifiers. DAO 3.5 and
// its Jet 3.5 engine came with the game's installer and are not part of Windows; DAO 3.6 and Jet 4.0 are, in 32-bit
// Windows, with the same interfaces and the same license key, and Jet 4.0 opens a Jet 3 database as it is. This points
// the game's own requests for a DAO 3.5 class at the DAO 3.6 class in its place, through the game executable's imports
// of CoGetClassObject, CoCreateInstance and CoCreateInstanceEx from ole32.dll. Fatal when it imports none of them, and
// when a redirected class is not registered, which is a Windows without DAO 3.6.
void installDAORedirect();
