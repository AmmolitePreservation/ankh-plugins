#pragma once

// The fixture acts as a game built with DAO 3.5: it asks COM for DAO 3.5's DBEngine class, makes an engine with the
// key DAO's classes are licensed with, and reads its Version. Each step is one line in fixtureReport.txt
// (fixtureReport.h): "daoClassObject result=<HRESULT>", then "daoEngine version=<version>" and "ready", or "failed".
// Returns 0, or the process's exit code.
int runDAOMode();
