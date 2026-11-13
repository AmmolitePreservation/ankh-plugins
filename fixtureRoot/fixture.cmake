# A game that builds its own executable: the tests run the loader, this root's echo plugin and the repository's
# plugins in it. Included from a fixture game's game.cmake.
file(GLOB fixtureGameSources CONFIGURE_DEPENDS "${rootDirectory}/fixtureGame/source/*.cpp")
set(fixtureGame "${game}.fixtureGame")
add_executable(${fixtureGame} WIN32 EXCLUDE_FROM_ALL ${fixtureGameSources})
set_target_properties(${fixtureGame} PROPERTIES OUTPUT_NAME fixtureGame RUNTIME_OUTPUT_DIRECTORY "${gameBinaryDirectory}")
add_dependencies(${game} ${fixtureGame})
applyCompileSettings(${fixtureGame})
applyLinkSettings(${fixtureGame})
if(gameLoader STREQUAL "version")
  target_compile_definitions(${fixtureGame} PRIVATE fixtureImportsVersion)
elseif(gameLoader STREQUAL "opengl32")
  target_compile_definitions(${fixtureGame} PRIVATE fixtureImportsOpenGL)
else()
  message(FATAL_ERROR "The fixture game has no call into ${gameLoader}.dll (${rootDirectory}/fixtureGame/source/main.cpp)")
endif()
# The Windows SDK's import library: the executable imports the system DLL, as a game does.
target_link_libraries(${fixtureGame} PRIVATE ${gameLoader})
# Its DAO mode asks ole32 for COM classes through its own imports, as a game built with DAO 3.5 does.
target_link_libraries(${fixtureGame} PRIVATE ole32 oleaut32)
# A game imports Direct3D 9 and DirectInput 8, or loads them itself after it starts, which a plugin sees as a
# module load. A fixture game's game.cmake sets fixtureLoadsLibrariesAtRunTime for the second kind.
if(fixtureLoadsLibrariesAtRunTime)
  target_compile_definitions(${fixtureGame} PRIVATE fixtureLoadsLibrariesAtRunTime)
else()
  target_link_libraries(${fixtureGame} PRIVATE d3d9 dinput8)
endif()
# A fixture game whose game.cfg names a launcher gets one that imports msimg32.dll, which launcher-skip stands in for.
if(gameLauncher)
  file(GLOB fixtureLauncherSources CONFIGURE_DEPENDS "${rootDirectory}/fixtureLauncher/source/*.cpp")
  get_filename_component(fixtureLauncherName "${gameLauncher}" NAME_WE)
  add_executable(${game}.fixtureLauncher WIN32 EXCLUDE_FROM_ALL ${fixtureLauncherSources})
  set_target_properties(${game}.fixtureLauncher PROPERTIES OUTPUT_NAME "${fixtureLauncherName}"
    RUNTIME_OUTPUT_DIRECTORY "${gameBinaryDirectory}")
  add_dependencies(${game} ${game}.fixtureLauncher)
  applyCompileSettings(${game}.fixtureLauncher)
  applyLinkSettings(${game}.fixtureLauncher)
  target_link_libraries(${game}.fixtureLauncher PRIVATE msimg32)
endif()
if(NOT "${gameTimeDateStamp}" STREQUAL "")
  # /Brepro makes the linker's timeDateStamp a hash of the executable's content, while game.cfg names one build.
  add_custom_command(TARGET ${fixtureGame} POST_BUILD VERBATIM
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${rootDirectory}/setTimeDateStamp.ps1"
      -path "$<TARGET_FILE:${fixtureGame}>" -timeDateStamp "${gameTimeDateStamp}")
endif()
