#include "sessionLog.h"

#include "fatalError.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

static const char* const logsFolderName = "logs";
static const char* const logFileExtension = ".log";
static const unsigned long long retentionDays = 90;
static const unsigned long long fileTimeTicksPerDay = 24ULL * 60ULL * 60ULL * 10000000ULL;

// The file is opened for each write and closed after it, so a tool reading it while the game runs (most zip tools open
// a file without sharing writes) can. A line that comes while such a reader has it waits here and is written, in order,
// with the next line that gets in.
static const size_t pendingLimit = 1024 * 1024;
static std::string sessionLogPath;
static std::string pendingLines;
static unsigned long droppedLineCount = 0;
// Hooks on any thread can log, so lines are written one at a time.
static CRITICAL_SECTION sessionLogLock;

static unsigned long long fileTimeToTicks(const FILETIME& fileTime) {
  return (static_cast<unsigned long long>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
}

void removeExpiredLogs(const std::string& logDirectory, const std::string& logFilePrefix) {
  FILETIME nowFileTime;
  GetSystemTimeAsFileTime(&nowFileTime);
  const unsigned long long expiryTicks = fileTimeToTicks(nowFileTime) - retentionDays * fileTimeTicksPerDay;

  const std::string searchPattern = logDirectory + "\\" + logFilePrefix + "*" + logFileExtension;
  WIN32_FIND_DATAA foundFile;
  HANDLE searchHandle = FindFirstFileA(searchPattern.c_str(), &foundFile);
  if (searchHandle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
    return;
  }
  if (searchHandle == INVALID_HANDLE_VALUE) {
    fatalError("Could not enumerate the logs in %s (error %lu)", logDirectory.c_str(), GetLastError());
  }
  do {
    if (fileTimeToTicks(foundFile.ftLastWriteTime) >= expiryTicks) {
      continue;
    }
    const std::string expiredPath = logDirectory + "\\" + foundFile.cFileName;
    if (DeleteFileA(expiredPath.c_str())) {
      writeLogLine("INFO", "expiredLogRemoved file=%s", foundFile.cFileName);
    } else {
      writeLogLine("WARN", "expiredLogNotRemoved file=%s error=%lu", foundFile.cFileName, GetLastError());
    }
  } while (FindNextFileA(searchHandle, &foundFile));
  const DWORD enumerationError = GetLastError();
  FindClose(searchHandle);
  if (enumerationError != ERROR_NO_MORE_FILES) {
    fatalError("Listing the logs in %s stopped early (error %lu)", logDirectory.c_str(), enumerationError);
  }
}

std::string createLogFolder(const std::string& pluginDirectory, const std::string& folderName) {
  const std::string logsDirectory = pluginDirectory + "\\" + logsFolderName;
  const std::string logFolder = logsDirectory + "\\" + folderName;
  for (const std::string& directory : { logsDirectory, logFolder }) {
    if (!CreateDirectoryA(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
      fatalError("Could not create the log folder %s (error %lu)", directory.c_str(), GetLastError());
    }
  }
  return logFolder;
}

static std::string timestampedLine(const char* level, const char* message) {
  SYSTEMTIME now;
  GetLocalTime(&now);
  char line[1280];
  const int lineLength = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s %s\r\n",
    now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, level, message);
  if (lineLength < 0 || static_cast<size_t>(lineLength) >= sizeof(line)) {
    return std::string();
  }
  return std::string(line, static_cast<size_t>(lineLength));
}

// Under the lock: writes the waiting lines. False when the file could not take them for any reason but a reader
// holding it.
static bool writePendingLines() {
  const HANDLE file = CreateFileA(sessionLogPath.c_str(), FILE_APPEND_DATA,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return GetLastError() == ERROR_SHARING_VIOLATION;
  }
  if (droppedLineCount != 0) {
    char message[64];
    snprintf(message, sizeof(message), "logLinesDropped count=%lu", droppedLineCount);
    pendingLines += timestampedLine("WARN", message);
    droppedLineCount = 0;
  }
  DWORD bytesWritten = 0;
  const BOOL succeeded = WriteFile(file, pendingLines.data(), static_cast<DWORD>(pendingLines.size()), &bytesWritten,
    nullptr);
  const DWORD error = GetLastError();
  CloseHandle(file);
  if (!succeeded || bytesWritten != static_cast<DWORD>(pendingLines.size())) {
    SetLastError(error);
    return false;
  }
  pendingLines.clear();
  return true;
}

void openSessionLog(const std::string& pluginDirectory, const char* pluginName, const char* sessionStamp) {
  if (!sessionLogPath.empty()) {
    fatalError("openSessionLog called while a session log is already open");
  }

  const std::string logFilePrefix = std::string(pluginName) + "_";
  const std::string logDirectory = createLogFolder(pluginDirectory, pluginName);

  char fileName[128];
  snprintf(fileName, sizeof(fileName), "%s%s_pid%lu%s", logFilePrefix.c_str(), sessionStamp, GetCurrentProcessId(),
    logFileExtension);

  const std::string logPath = logDirectory + "\\" + fileName;
  const HANDLE file = CreateFileA(logPath.c_str(), FILE_APPEND_DATA,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    fatalError("Could not open session log %s (error %lu)", fileName, GetLastError());
  }
  CloseHandle(file);
  InitializeCriticalSection(&sessionLogLock);
  sessionLogPath = logPath;

  removeExpiredLogs(logDirectory, logFilePrefix);
}

void closeSessionLog() {
  if (sessionLogPath.empty()) {
    fatalError("closeSessionLog called while no session log is open");
  }
  EnterCriticalSection(&sessionLogLock);
  if (!pendingLines.empty()) {
    writePendingLines();
  }
  sessionLogPath.clear();
  pendingLines.clear();
  LeaveCriticalSection(&sessionLogLock);
  DeleteCriticalSection(&sessionLogLock);
}

bool tryWriteLogLine(const char* level, const char* message) {
  if (sessionLogPath.empty()) {
    return false;
  }
  const std::string line = timestampedLine(level, message);
  if (line.empty()) {
    return false;
  }
  EnterCriticalSection(&sessionLogLock);
  if (pendingLines.size() + line.size() > pendingLimit) {
    droppedLineCount++;
  } else {
    pendingLines += line;
  }
  const bool succeeded = writePendingLines();
  LeaveCriticalSection(&sessionLogLock);
  return succeeded;
}

void writeLogLine(const char* level, const char* format, ...) {
  char message[1024];
  va_list arguments;
  va_start(arguments, format);
  const int messageLength = vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  if (messageLength < 0 || static_cast<size_t>(messageLength) >= sizeof(message)) {
    fatalError("Log message did not fit the line buffer (format: %s)", format);
  }
  if (!tryWriteLogLine(level, message)) {
    fatalError("Could not write to the session log (error %lu)", GetLastError());
  }
}
