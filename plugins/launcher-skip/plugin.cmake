# Built only for a game with a launcher (game.cfg launcher=), as msimg32.dll, which the launcher imports: it starts
# the game in the launcher's place. The loader does not load it, so it has no Load and no PluginInterfaceVersion.
if(gameLauncher)
  string(REPLACE "|" ";" gameExecutableList "${gameExecutables}")
  list(GET gameExecutableList 0 gameExecutable)
  addCoreLibrary(${pluginName} msimg32 "${pluginFolder}"
    SOURCES "${pluginFolder}/source/exports.def" "${coreDirectory}/loader/source/entryPointHook.cpp")
  target_include_directories(${pluginName} PRIVATE "${coreDirectory}/loader/source")
  target_compile_definitions(${pluginName} PRIVATE "ankhPluginsGameExecutable=\"${gameExecutable}\""
    "ankhPluginsLauncherExecutable=\"${gameLauncher}\"")
endif()
