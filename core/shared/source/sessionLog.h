#pragma once

#include <windows.h>

#include <string>

// Everything the plugins generate goes under logs\ in the ankh-plugins folder (pluginDirectory):
// logs\<folderName>\, created when missing, which this returns.
std::string createLogFolder(const std::string& pluginDirectory, const std::string& folderName);

// Deletes the <logFilePrefix>*.log files in logDirectory last written before the retention period (90 days).
void removeExpiredLogs(const std::string& logDirectory, const std::string& logFilePrefix);

// Opens logs\<pluginName>\<pluginName>_<sessionStamp>_pid<processID>.log for this game session,
// appending when a reloaded plugin reopens it, then removes that plugin's session logs past
// the retention period. The file is open only while a line is written, so other programs can read and copy it.
void openSessionLog(const std::string& pluginDirectory, const char* pluginName, const char* sessionStamp);

// Ends the session log, for a plugin that is being unloaded.
void closeSessionLog();

// Writes one timestamped line; while a reader holds the file shut for writing, the line waits and goes with the next
// one (past 1 MB waiting, lines are counted and the count is logged). Terminates the process if the write fails.
void writeLogLine(const char* level, const char* format, ...);

// Writes one timestamped line with a preformatted message. Returns false when
// no log is open or the write fails. Only fatalError should need this.
bool tryWriteLogLine(const char* level, const char* message);
