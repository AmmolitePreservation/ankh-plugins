#include "gameDefinition.h"

#include "fatalError.h"
#include "settingsFile.h"

#include <cstdlib>

static const char* const gameFolderName = "game";
static const char* const gameFileName = "game.cfg";
static const size_t anyNumber = static_cast<size_t>(-1);

struct GameSetting {
  const char* key;
  size_t fewest;
  size_t most;
  std::vector<std::string> values;
};

std::string gameDefinitionPath(const std::string& pluginsDirectory, const char* fileName) {
  return pluginsDirectory + "\\" + gameFolderName + "\\" + fileName;
}

std::string readGameDefinitionFile(const std::string& pluginsDirectory, const char* fileName) {
  const std::string path = gameDefinitionPath(pluginsDirectory, fileName);
  std::string contents;
  const std::string problem = readWholeFile(path, path, contents);
  if (!problem.empty()) {
    fatalError("%s. The loader's package puts this file there; install it again.", problem.c_str());
  }
  return contents;
}

static std::string fileNameOf(const std::string& path) {
  const size_t separator = path.find_last_of("\\/");
  return separator == std::string::npos ? path : path.substr(separator + 1);
}

static unsigned long parseTimeDateStamp(const std::string& path, const std::string& text) {
  const bool isWellFormed = text.size() == 10 && text.compare(0, 2, "0x") == 0 &&
    text.find_first_not_of("0123456789ABCDEF", 2) == std::string::npos;
  if (!isWellFormed) {
    fatalError("%s: timeDateStamp must be 0x and eight hexadecimal digits in capitals, not %s", path.c_str(), text.c_str());
  }
  return strtoul(text.c_str(), nullptr, 16);
}

GameDefinition readGameDefinition(const std::string& pluginsDirectory) {
  const std::string path = gameDefinitionPath(pluginsDirectory, gameFileName);
  GameSetting executable = { "executable", 1, 1, {} };
  // Picks the build, so nothing reads it once the build runs.
  GameSetting architecture = { "architecture", 1, 1, {} };
  GameSetting loader = { "loader", 1, 1, {} };
  GameSetting companion = { "companion", 0, anyNumber, {} };
  GameSetting launcher = { "launcher", 0, 1, {} };
  GameSetting timeDateStamp = { "timeDateStamp", 0, 1, {} };
  GameSetting* const settings[] = { &executable, &architecture, &loader, &companion, &launcher, &timeDateStamp };
  const std::vector<std::string> lines = splitLines(readGameDefinitionFile(pluginsDirectory, gameFileName));
  for (size_t index = 0; index < lines.size(); index++) {
    const std::string line = trimWhitespace(lines[index]);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const size_t separator = line.find('=');
    const std::string key = trimWhitespace(line.substr(0, separator));
    const std::string value = separator == std::string::npos ? "" : trimWhitespace(line.substr(separator + 1));
    GameSetting* setting = nullptr;
    for (GameSetting* candidate : settings) {
      if (key == candidate->key) {
        setting = candidate;
      }
    }
    if (setting == nullptr || value.empty()) {
      fatalError("%s line %zu is not a setting of a game: %s", path.c_str(), index + 1, line.c_str());
    }
    setting->values.push_back(value);
  }
  for (const GameSetting* setting : settings) {
    if (setting->values.size() < setting->fewest) {
      fatalError("%s does not set %s", path.c_str(), setting->key);
    }
    if (setting->values.size() > setting->most) {
      fatalError("%s sets %s more than once", path.c_str(), setting->key);
    }
  }
  GameDefinition definition = {};
  definition.executableName = fileNameOf(executable.values[0]);
  definition.loaderName = loader.values[0];
  definition.companionNames = companion.values;
  definition.launcherName = launcher.values.empty() ? "" : launcher.values[0];
  definition.namesBuild = !timeDateStamp.values.empty();
  definition.timeDateStamp = definition.namesBuild ? parseTimeDateStamp(path, timeDateStamp.values[0]) : 0;
  return definition;
}
