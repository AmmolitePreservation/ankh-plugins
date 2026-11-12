#pragma once

#include <string>
#include <vector>

// The game an install's plugins are for: ankh-plugins\game holds the game.cfg and plugins.txt of the game's folder in
// its root (games\<game>), which the loader's package ships. A newer package replaces both; neither is a player's.
struct GameDefinition {
  // File names: the folder game.cfg's executable= may carry is the place of the loader, which is how this was found.
  std::string executableName;
  std::vector<std::string> companionNames;
  // Empty for a game without a launcher.
  std::string launcherName;
  // The system DLL the game's loader stands in for, without .dll.
  std::string loaderName;
  bool namesBuild;
  unsigned long timeDateStamp;
};

// <pluginsDirectory>\game\<fileName>
std::string gameDefinitionPath(const std::string& pluginsDirectory, const char* fileName);

// The contents of <pluginsDirectory>\game\<fileName>. Fatal when it cannot be read.
std::string readGameDefinitionFile(const std::string& pluginsDirectory, const char* fileName);

// Reads <pluginsDirectory>\game\game.cfg. Fatal when it cannot be read, a line is not one of game.cfg's settings, or a
// setting is given more or fewer times than game.cfg allows.
GameDefinition readGameDefinition(const std::string& pluginsDirectory);
