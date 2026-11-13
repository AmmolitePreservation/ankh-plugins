#include "fixtureReport.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

static const char* const reportFileName = "fixtureReport.txt";

static FILE* report = nullptr;

bool openReport() {
  std::string path(MAX_PATH, '\0');
  const DWORD length = GetModuleFileNameA(nullptr, path.data(), MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return false;
  }
  path.resize(length);
  path.resize(path.rfind('\\') + 1);
  path += reportFileName;
  report = fopen(path.c_str(), "w");
  return report != nullptr;
}

void writeReport(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  vfprintf(report, format, arguments);
  va_end(arguments);
  fputc('\n', report);
  fflush(report);
}

bool failStep(const char* step, unsigned long error) {
  writeReport("failed step=%s error=0x%08lX", step, error);
  return false;
}
