#pragma once

// fixtureReport.txt beside the executable, which a mode of the fixture game writes one "event key=value ..." line to
// for each step it takes, for the tests to read.

// Creates the report, empty. False when it cannot.
bool openReport();

void writeReport(const char* format, ...);

// Writes "failed step=<step> error=<error>" and returns false.
bool failStep(const char* step, unsigned long error);
