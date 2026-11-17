#include "serverAddress.h"

#include "fatalError.h"
#include "sessionLog.h"

#include <windows.h>

#include <cctype>
#include <cstdlib>

static const char* const addressVariable = "ankhPluginsServerAddress";
static const size_t longestHostName = 253;

static std::string address;
static std::string hostName;
static unsigned short port = 0;

static bool isHostName(const std::string& text) {
  if (text.empty() || text.size() > longestHostName) {
    return false;
  }
  for (const char letter : text) {
    if (!std::isalnum(static_cast<unsigned char>(letter)) && letter != '.' && letter != '-') {
      return false;
    }
  }
  return true;
}

static bool isPort(const std::string& text) {
  if (text.empty() || text.size() > 5) {
    return false;
  }
  for (const char letter : text) {
    if (!std::isdigit(static_cast<unsigned char>(letter))) {
      return false;
    }
  }
  const int number = std::atoi(text.c_str());
  return number >= 1 && number <= 65535;
}

void loadServerAddress() {
  char value[512];
  const DWORD length = GetEnvironmentVariableA(addressVariable, value, sizeof(value));
  if (length >= sizeof(value)) {
    fatalError("%s is longer than %zu characters", addressVariable, sizeof(value) - 1);
  }
  if (length == 0) {
    writeLogLine("INFO", "serverAddress source=client");
    return;
  }
  const std::string text(value, length);
  const size_t colon = text.find(':');
  const bool isValid = colon == std::string::npos ? isHostName(text)
    : isHostName(text.substr(0, colon)) && isPort(text.substr(colon + 1));
  if (!isValid) {
    fatalError("%s is not a host name with an optional port", addressVariable);
  }
  address = text;
  hostName = text.substr(0, colon);
  port = colon == std::string::npos ? 0 : static_cast<unsigned short>(std::atoi(text.c_str() + colon + 1));
  writeLogLine("INFO", "serverAddress source=launcher address=%s", address.c_str());
}

const std::string& serverAddress() {
  return address;
}

const std::string& serverHostName() {
  return hostName;
}

unsigned short serverPort() {
  return port;
}
