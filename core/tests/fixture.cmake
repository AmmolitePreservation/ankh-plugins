# A game that builds its own executable: the loader's tests run the loader and the echo plugin in it.
add_executable(fixtureGame WIN32 "${coreDirectory}/tests/fixtureGame/source/main.cpp")
applyBuildSettings(fixtureGame)
target_link_libraries(fixtureGame PRIVATE version)
addPluginLibrary(echo "${coreDirectory}/tests/echo")
