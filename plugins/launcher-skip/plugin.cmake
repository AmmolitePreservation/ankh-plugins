# msimg32.dll, which a game's launcher imports: it starts the game in the launcher's place. It goes beside the
# executable of a game with a launcher (game.cfg launcher=) and reads which launcher and which game from the install.
# The loader does not load it, so it has no Load and no PluginInterfaceVersion. Its stubs are x86 only.
if(buildArchitecture STREQUAL "x86")
  addCentralLibrary(${pluginName} msimg32 "${pluginFolder}"
    SOURCES "${pluginFolder}/source/exports.def" "${coreDirectory}/loader/source/entryPointHook.cpp")
  target_include_directories(${pluginName} PRIVATE "${coreDirectory}/loader/source")
endif()
