# A game that builds its own executable: the loader's tests run the loader and the echo plugin in it.
add_executable(fixtureGame WIN32 "${coreDirectory}/tests/fixtureGame/source/main.cpp")
applyBuildSettings(fixtureGame)
if(gameLoader STREQUAL "version")
  target_compile_definitions(fixtureGame PRIVATE fixtureImportsVersion)
elseif(gameLoader STREQUAL "opengl32")
  target_compile_definitions(fixtureGame PRIVATE fixtureImportsOpenGL)
else()
  message(FATAL_ERROR "The fixture game has no call into ${gameLoader}.dll (core/tests/fixtureGame/source/main.cpp)")
endif()
# The Windows SDK's import library: the executable imports the system DLL, as a game does.
target_link_libraries(fixtureGame PRIVATE ${gameLoader})
addPluginLibrary(echo "${coreDirectory}/tests/echo")
