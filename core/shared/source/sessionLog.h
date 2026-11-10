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
// the retention period.
void openSessionLog(const std::string& pluginDirectory, const char* pluginName, const char* sessionStamp);

// Closes the session log, for a plugin that is being unloaded.
void closeSessionLog();

// Writes one timestamped line. Terminates the process if the write fails.
void writeLogLine(const char* level, const char* format, ...);

// Writes one timestamped line with a preformatted message. Returns false when
// no log is open or the write fails. Only fatalError should need this.
bool tryWriteLogLine(const char* level, const char* message);

// Lets the WriteFile hook recognize this DLL's own log writes and leave them untimed.
bool isSessionLogHandle(HANDLE handle);
