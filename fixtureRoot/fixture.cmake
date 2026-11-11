# A game that builds its own executable: the loader's tests run the loader and this root's echo plugin in it.
add_executable(fixtureGame WIN32 "${rootDirectory}/fixtureGame/source/main.cpp")
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
if(NOT "${gameTimeDateStamp}" STREQUAL "")
  # /Brepro makes the linker's timeDateStamp a hash of the executable's content, while game.cfg names one build.
  add_custom_command(TARGET fixtureGame POST_BUILD VERBATIM
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${rootDirectory}/setTimeDateStamp.ps1"
      -path "$<TARGET_FILE:fixtureGame>" -timeDateStamp "${gameTimeDateStamp}")
endif()
