# A game that builds its own executable: the tests run the loader, this root's echo plugin and the repository's
# plugins in it.
file(GLOB fixtureGameSources CONFIGURE_DEPENDS "${rootDirectory}/fixtureGame/source/*.cpp")
add_executable(fixtureGame WIN32 ${fixtureGameSources})
applyBuildSettings(fixtureGame)
if(gameLoader STREQUAL "version")
  target_compile_definitions(fixtureGame PRIVATE fixtureImportsVersion)
elseif(gameLoader STREQUAL "opengl32")
  target_compile_definitions(fixtureGame PRIVATE fixtureImportsOpenGL)
else()
  message(FATAL_ERROR "The fixture game has no call into ${gameLoader}.dll (${rootDirectory}/fixtureGame/source/main.cpp)")
endif()
# The Windows SDK's import library: the executable imports the system DLL, as a game does.
target_link_libraries(fixtureGame PRIVATE ${gameLoader})
# A game imports Direct3D 9 and DirectInput 8, or loads them itself after it starts, which a plugin sees as a
# module load. A fixture game's game.cmake sets fixtureLoadsLibrariesAtRunTime for the second kind.
if(fixtureLoadsLibrariesAtRunTime)
  target_compile_definitions(fixtureGame PRIVATE fixtureLoadsLibrariesAtRunTime)
else()
  target_link_libraries(fixtureGame PRIVATE d3d9 dinput8)
endif()
if(NOT "${gameTimeDateStamp}" STREQUAL "")
  # /Brepro makes the linker's timeDateStamp a hash of the executable's content, while game.cfg names one build.
  add_custom_command(TARGET fixtureGame POST_BUILD VERBATIM
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${rootDirectory}/setTimeDateStamp.ps1"
      -path "$<TARGET_FILE:fixtureGame>" -timeDateStamp "${gameTimeDateStamp}")
endif()
