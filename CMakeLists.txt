cmake_minimum_required(VERSION 3.25)
project(ankhPlugins C CXX ASM_MASM)

if(NOT MSVC)
  message(FATAL_ERROR "ankh-plugins builds Windows DLLs with MSVC; configure it from a Visual Studio environment (build.ps1 does)")
endif()
if(gameArchitecture STREQUAL "x64")
  set(expectedPointerSize 8)
elseif(gameArchitecture STREQUAL "x86")
  set(expectedPointerSize 4)
else()
  message(FATAL_ERROR "gameArchitecture must be x64 or x86 (build.ps1 sets it from game.cfg)")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL expectedPointerSize)
  message(FATAL_ERROR "the compiler builds for a different architecture than ${gameArchitecture}; configure from the Visual Studio ${gameArchitecture} environment (build.ps1 does)")
endif()
if(NOT DEFINED game)
  message(FATAL_ERROR "game is not set (build.ps1 sets it from -game)")
endif()
set(gameDirectory "${CMAKE_SOURCE_DIR}/games/${game}")
if(NOT EXISTS "${gameDirectory}/game.cfg")
  message(FATAL_ERROR "games/${game}/game.cfg does not exist")
endif()
if(NOT DEFINED gameExecutables)
  message(FATAL_ERROR "gameExecutables is not set (build.ps1 sets it from game.cfg)")
endif()
if(NOT DEFINED ankhPluginsBuildCommit)
  message(FATAL_ERROR "ankhPluginsBuildCommit is not set (build.ps1 sets it from git)")
endif()

# Flags are set per target below, so CMake's defaults (exceptions on, a per-configuration
# optimization level) do not fight them.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(FATAL_ERROR "configure with -DCMAKE_BUILD_TYPE=Release (build.ps1 does)")
endif()
set(CMAKE_C_FLAGS "/DWIN32 /D_WINDOWS")
set(CMAKE_CXX_FLAGS "/DWIN32 /D_WINDOWS")
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")
# The static runtime leaves DLLs every Windows install already has as the only dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(outputDirectory "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${outputDirectory}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${outputDirectory}")
# Import libraries go elsewhere: the loader's version.lib in the link directory would shadow
# the Windows SDK's.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(coreDirectory "${CMAKE_SOURCE_DIR}/core")
set(minhookDirectory "${coreDirectory}/shared/vendor/minhook")
add_library(minhook STATIC
  "${minhookDirectory}/src/buffer.c"
  "${minhookDirectory}/src/hook.c"
  "${minhookDirectory}/src/trampoline.c"
  "${minhookDirectory}/src/hde/hde$<IF:$<STREQUAL:${gameArchitecture},x64>,64,32>.c")
target_include_directories(minhook PUBLIC "${minhookDirectory}/include" PRIVATE "${minhookDirectory}/src")
target_compile_options(minhook PRIVATE /O2 /Gy /Brepro)

file(GLOB sharedSources CONFIGURE_DEPENDS "${coreDirectory}/shared/source/*.cpp")
file(GLOB pluginInterfaceSources CONFIGURE_DEPENDS "${coreDirectory}/plugin/source/*.cpp")

# embedDefaultFiles(<targetName> <settings file>...): the library carries its settings files, embedded at build
# time, and writes one out when the ankh-plugins folder lacks it, so a release ships no settings files.
function(embedDefaultFiles targetName)
  set(outputPath "${CMAKE_BINARY_DIR}/generated/${targetName}DefaultFiles.cpp")
  list(LENGTH ARGN defaultFileCount)
  set(fileArguments "")
  set(index 0)
  foreach(sourcePath IN LISTS ARGN)
    list(APPEND fileArguments "-DdefaultFile${index}=${sourcePath}")
    math(EXPR index "${index} + 1")
  endforeach()
  add_custom_command(OUTPUT "${outputPath}"
    COMMAND "${CMAKE_COMMAND}" "-DoutputPath=${outputPath}" "-DdefaultFileCount=${defaultFileCount}" ${fileArguments}
      -P "${CMAKE_SOURCE_DIR}/cmake/embedDefaultFiles.cmake"
    DEPENDS ${ARGN} "${CMAKE_SOURCE_DIR}/cmake/embedDefaultFiles.cmake"
    VERBATIM)
  target_sources(${targetName} PRIVATE "${outputPath}")
endfunction()

function(applyBuildSettings targetName)
  # /Brepro leaves no timestamps, so the same source builds the same bytes and a release only moves when its code does.
  target_link_options(${targetName} PRIVATE /OPT:REF /OPT:ICF /Brepro)
  # MSVC deprecates the bounded C string functions the code uses in favor of its _s variants.
  target_compile_definitions(${targetName} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
    "ankhPluginsBuildCommit=\"${ankhPluginsBuildCommit}\"")
  target_compile_options(${targetName} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /EHs-c- /GR- /Gy /utf-8 /O2 /Brepro>)
endfunction()

# addCoreLibrary(<targetName> <outputName> <folder> [SOURCES <extra source>...] [SETTINGS <extra settings file>...]):
# a DLL from <folder>/source. Each compiles all of shared/source; function-level linking lets the linker drop what
# it does not use. Its settings files are the .cfg files in its folder plus any SETTINGS.
function(addCoreLibrary targetName outputName folder)
  cmake_parse_arguments(PARSE_ARGV 3 library "" "" "SOURCES;SETTINGS")
  file(GLOB librarySources CONFIGURE_DEPENDS "${folder}/source/*.cpp")
  add_library(${targetName} SHARED ${sharedSources} ${librarySources} ${library_SOURCES})
  set_target_properties(${targetName} PROPERTIES OUTPUT_NAME ${outputName})
  target_include_directories(${targetName} PRIVATE "${coreDirectory}/shared/source")
  target_link_libraries(${targetName} PRIVATE minhook)
  applyBuildSettings(${targetName})
  # A game's own copy of a settings file (games/<game>/<file>.cfg) replaces the shipped defaults.
  file(GLOB settingsFiles CONFIGURE_DEPENDS "${folder}/*.cfg")
  set(embeddedSettingsFiles "")
  foreach(settingsFile IN LISTS settingsFiles)
    get_filename_component(settingsFileName "${settingsFile}" NAME)
    if(EXISTS "${gameDirectory}/${settingsFileName}")
      list(APPEND embeddedSettingsFiles "${gameDirectory}/${settingsFileName}")
    else()
      list(APPEND embeddedSettingsFiles "${settingsFile}")
    endif()
  endforeach()
  embedDefaultFiles(${targetName} ${embeddedSettingsFiles} ${library_SETTINGS})
endfunction()

# addPluginLibrary(<pluginName> <pluginFolder> [SOURCES ...] [SETTINGS ...]): <pluginName>.dll, a plugin the loader
# loads. core/plugin/source gives it the PluginInterfaceVersion export the loader checks before calling Load.
function(addPluginLibrary pluginName pluginFolder)
  cmake_parse_arguments(PARSE_ARGV 2 plugin "" "" "SOURCES;SETTINGS")
  addCoreLibrary(${pluginName} ${pluginName} "${pluginFolder}"
    SOURCES ${pluginInterfaceSources} ${plugin_SOURCES}
    SETTINGS ${plugin_SETTINGS})
endfunction()

# The loader carries the game's plugin list, so each game gets its own build of it.
# x64 has no inline assembly, so its version.dll stubs are versionStubs.asm; x86's are in versionProxy.cpp.
set(versionStubs "")
if(gameArchitecture STREQUAL "x64")
  set(versionStubs "${coreDirectory}/loader/source/versionStubs.asm")
endif()
addCoreLibrary(loader version "${coreDirectory}/loader"
  SOURCES "${coreDirectory}/loader/source/exports.def" ${versionStubs}
  SETTINGS "${gameDirectory}/plugins.txt")
target_compile_definitions(loader PRIVATE "ankhPluginsGameExecutables=\"${gameExecutables}\"")

# addPluginFolders(<parentFolder>): every folder in it is a plugin named after the folder. One that needs more than
# addPluginLibrary gives carries a plugin.cmake, which is included in its place and sees pluginName and pluginFolder.
function(addPluginFolders parentFolder)
  file(GLOB pluginFolders LIST_DIRECTORIES true "${parentFolder}/*")
  foreach(pluginFolder IN LISTS pluginFolders)
    if(IS_DIRECTORY "${pluginFolder}")
      get_filename_component(pluginName "${pluginFolder}" NAME)
      if(EXISTS "${pluginFolder}/plugin.cmake")
        include("${pluginFolder}/plugin.cmake")
      else()
        addPluginLibrary(${pluginName} "${pluginFolder}")
      endif()
    endif()
  endforeach()
endfunction()

# Every central plugin is built for every game; a game's plugins.txt names the ones it loads. A game's own plugins
# live in games/<game>/plugins/<plugin>, built the same way.
addPluginFolders("${CMAKE_SOURCE_DIR}/plugins")
addPluginFolders("${gameDirectory}/plugins")

# What a game builds beyond plugins.
if(EXISTS "${gameDirectory}/game.cmake")
  include("${gameDirectory}/game.cmake")
endif()
