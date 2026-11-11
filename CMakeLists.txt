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
if(NOT DEFINED rootDirectory)
  message(FATAL_ERROR "rootDirectory is not set (build.ps1 sets it from -root)")
endif()
# Real paths, so a root that is this repository under another spelling of its path is still this repository.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" repositoryDirectory)
file(REAL_PATH "${rootDirectory}" rootDirectory)
set(gameDirectory "${rootDirectory}/games/${game}")
if(NOT EXISTS "${gameDirectory}/game.cfg")
  message(FATAL_ERROR "${gameDirectory}/game.cfg does not exist")
endif()
if(NOT DEFINED gameExecutables)
  message(FATAL_ERROR "gameExecutables is not set (build.ps1 sets it from game.cfg)")
endif()
if(NOT DEFINED gameLoader)
  message(FATAL_ERROR "gameLoader is not set (build.ps1 sets it from game.cfg)")
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
# Import libraries go elsewhere: the loader's has the name of a Windows SDK one and would shadow
# it in the link directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(coreDirectory "${repositoryDirectory}/core")
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
      -P "${repositoryDirectory}/cmake/embedDefaultFiles.cmake"
    DEPENDS ${ARGN} "${repositoryDirectory}/cmake/embedDefaultFiles.cmake"
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
  string(FIND "${folder}/" "${rootDirectory}/games/" gamesFolderPosition)
  if(NOT gamesFolderPosition EQUAL 0)
    set_property(GLOBAL APPEND PROPERTY centralTargets ${targetName})
  endif()
endfunction()

# addPluginLibrary(<pluginName> <pluginFolder> [SOURCES ...] [SETTINGS ...]): <pluginName>.dll, a plugin the loader
# loads. core/plugin/source gives it the PluginInterfaceVersion export the loader checks before calling Load.
# <pluginFolder>/handlers/<handler>.cpp is the part of the plugin a game may replace: when
# games/<game>/handlers/<pluginName>/<handler>.cpp exists it is compiled in its place, against the plugin's headers.
# A game that replaces a handler may give it more: the .cpp files of games/<game>/handlers/<pluginName>/source are
# compiled with it, and games/<game>/handlers/<pluginName>/<pluginName>.<part>.cfg are settings files of its own.
function(addPluginLibrary pluginName pluginFolder)
  cmake_parse_arguments(PARSE_ARGV 2 plugin "" "" "SOURCES;SETTINGS")
  set(gameHandlerFolder "${gameDirectory}/handlers/${pluginName}")
  file(GLOB defaultHandlers CONFIGURE_DEPENDS "${pluginFolder}/handlers/*.cpp")
  file(GLOB gameHandlers CONFIGURE_DEPENDS "${gameHandlerFolder}/*.cpp")
  file(GLOB gameHandlerParts CONFIGURE_DEPENDS "${gameHandlerFolder}/source/*.cpp")
  file(GLOB gameHandlerSettings CONFIGURE_DEPENDS "${gameHandlerFolder}/*.cfg")
  set(handlerNames "")
  set(handlerSources "")
  foreach(defaultHandler IN LISTS defaultHandlers)
    get_filename_component(handlerName "${defaultHandler}" NAME)
    list(APPEND handlerNames "${handlerName}")
    if(EXISTS "${gameHandlerFolder}/${handlerName}")
      list(APPEND handlerSources "${gameHandlerFolder}/${handlerName}")
    else()
      list(APPEND handlerSources "${defaultHandler}")
    endif()
  endforeach()
  foreach(gameHandler IN LISTS gameHandlers)
    get_filename_component(handlerName "${gameHandler}" NAME)
    if(NOT handlerName IN_LIST handlerNames)
      message(FATAL_ERROR "${gameHandler} replaces no handler: ${pluginFolder}/handlers has no ${handlerName}")
    endif()
  endforeach()
  if(NOT gameHandlers AND (gameHandlerParts OR gameHandlerSettings))
    message(FATAL_ERROR "${gameHandlerFolder} holds source or settings files and replaces no handler of ${pluginName}")
  endif()
  foreach(gameHandlerSetting IN LISTS gameHandlerSettings)
    get_filename_component(settingsFileName "${gameHandlerSetting}" NAME)
    string(FIND "${settingsFileName}" "${pluginName}." namePosition)
    if(NOT namePosition EQUAL 0 OR settingsFileName STREQUAL "${pluginName}.cfg")
      message(FATAL_ERROR "${gameHandlerSetting}: a handler's settings file is named ${pluginName}.<part>.cfg")
    endif()
  endforeach()
  addCoreLibrary(${pluginName} ${pluginName} "${pluginFolder}"
    SOURCES ${pluginInterfaceSources} ${handlerSources} ${gameHandlerParts} ${plugin_SOURCES}
    SETTINGS ${gameHandlerSettings} ${plugin_SETTINGS})
  target_include_directories(${pluginName} PRIVATE "${pluginFolder}/source")
  set_property(GLOBAL APPEND PROPERTY pluginNames ${pluginName})
endfunction()

# The loader carries the game's plugin list, so each game gets its own build of it, as the system DLL the game
# imports (game.cfg loader=). core/loader/proxies/<loader>.txt names that DLL's exports, one per line; the export
# table, the macro systemProxy.cpp expands and x64's stubs (it has no inline assembly; x86's are in systemProxy.cpp)
# are generated from it.
set(proxyListFile "${coreDirectory}/loader/proxies/${gameLoader}.txt")
if(NOT EXISTS "${proxyListFile}")
  message(FATAL_ERROR "${gameDirectory}/game.cfg: loader=${gameLoader}, but core/loader/proxies has no ${gameLoader}.txt")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${proxyListFile}")
file(STRINGS "${proxyListFile}" proxyExportNames)
list(LENGTH proxyExportNames proxyExportCount)
if(proxyExportCount EQUAL 0)
  message(FATAL_ERROR "${proxyListFile} names no export")
endif()
set(uniqueProxyExportNames ${proxyExportNames})
list(REMOVE_DUPLICATES uniqueProxyExportNames)
if(NOT "${uniqueProxyExportNames}" STREQUAL "${proxyExportNames}")
  message(FATAL_ERROR "${proxyListFile} names an export twice")
endif()
set(proxyMacroLines "")
set(proxyDefinitionLines "")
set(proxyExternLines "")
set(proxyStubLines "")
foreach(proxyExportName IN LISTS proxyExportNames)
  if(NOT proxyExportName MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "${proxyListFile}: \"${proxyExportName}\" is not an export name")
  endif()
  list(APPEND proxyMacroLines "  X(${proxyExportName})")
  list(APPEND proxyDefinitionLines "  ${proxyExportName}=proxy${proxyExportName}")
  list(APPEND proxyExternLines "EXTERN real${proxyExportName}:QWORD")
  list(APPEND proxyStubLines
    "proxy${proxyExportName} PROC\n  jmp QWORD PTR [real${proxyExportName}]\nproxy${proxyExportName} ENDP")
endforeach()
list(JOIN proxyMacroLines " \\\n" proxyMacroBody)
list(JOIN proxyDefinitionLines "\n" proxyDefinitionBody)
list(JOIN proxyExternLines "\n" proxyExternBody)
list(JOIN proxyStubLines "\n" proxyStubBody)
set(generatedDirectory "${CMAKE_BINARY_DIR}/generated")
file(CONFIGURE OUTPUT "${generatedDirectory}/proxyExports.h" @ONLY CONTENT
  "#pragma once\n\n#define proxyLibraryName \"${gameLoader}.dll\"\n\n#define proxyExports(X) \\\n${proxyMacroBody}\n")
file(CONFIGURE OUTPUT "${generatedDirectory}/proxyExports.def" @ONLY CONTENT
  "LIBRARY ${gameLoader}.dll\nEXPORTS\n${proxyDefinitionBody}\n")
set(proxyStubs "")
if(gameArchitecture STREQUAL "x64")
  set(proxyStubs "${generatedDirectory}/proxyStubs.asm")
  # A single jump, so the caller's arguments and return address reach the real function untouched.
  file(CONFIGURE OUTPUT "${proxyStubs}" @ONLY CONTENT
    "OPTION CASEMAP:NONE\n${proxyExternBody}\n\n.CODE\n${proxyStubBody}\n\nEND\n")
endif()
addCoreLibrary(loader ${gameLoader} "${coreDirectory}/loader"
  SOURCES "${generatedDirectory}/proxyExports.def" ${proxyStubs}
  SETTINGS "${gameDirectory}/plugins.txt")
target_include_directories(loader PRIVATE "${generatedDirectory}")
target_compile_definitions(loader PRIVATE "ankhPluginsGameExecutables=\"${gameExecutables}\"")
# Set only for a game whose game.cfg names the build of its executable, which the loader then requires.
if(NOT "${gameTimeDateStamp}" STREQUAL "")
  target_compile_definitions(loader PRIVATE "ankhPluginsGameTimeDateStamp=${gameTimeDateStamp}")
endif()

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

# Every central plugin is built for every game; a game's plugins.txt names the ones it loads. The central plugins are
# this repository's plugins/ and, for a root of its own (build.ps1 -root), that root's plugins/ as well. A game's own
# plugins live in games/<game>/plugins/<plugin>, built the same way.
set(centralPluginFolders "${repositoryDirectory}/plugins" "${rootDirectory}/plugins")
list(REMOVE_DUPLICATES centralPluginFolders)
foreach(centralPluginFolder IN LISTS centralPluginFolders)
  addPluginFolders("${centralPluginFolder}")
endforeach()
addPluginFolders("${gameDirectory}/plugins")

# What a game builds beyond plugins.
if(EXISTS "${gameDirectory}/game.cmake")
  include("${gameDirectory}/game.cmake")
endif()

get_property(pluginNames GLOBAL PROPERTY pluginNames)
file(GLOB handlerFolders LIST_DIRECTORIES true "${gameDirectory}/handlers/*")
foreach(handlerFolder IN LISTS handlerFolders)
  get_filename_component(handlerFolderName "${handlerFolder}" NAME)
  if(NOT handlerFolderName IN_LIST pluginNames)
    message(FATAL_ERROR "${handlerFolder} names no plugin this game builds with handlers")
  endif()
endforeach()

# Code under core/ and a plugins/ beside games/ is central and cannot reach into games/: its quoted includes name a
# file beside it or in an include directory, none climbs out of a folder, and no central target has an include
# directory under games/, this repository's or the root's.
set(centralSourceFiles "")
foreach(centralFolder IN ITEMS "${coreDirectory}" ${centralPluginFolders})
  file(GLOB_RECURSE centralFolderFiles
    "${centralFolder}/*.cpp" "${centralFolder}/*.h" "${centralFolder}/*.c" "${centralFolder}/*.hpp")
  list(APPEND centralSourceFiles ${centralFolderFiles})
endforeach()
list(FILTER centralSourceFiles EXCLUDE REGEX "/vendor/")
foreach(centralSourceFile IN LISTS centralSourceFiles)
  file(STRINGS "${centralSourceFile}" includeLines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
  foreach(includeLine IN LISTS includeLines)
    if(includeLine MATCHES "include[ \t]*\"[^\"]*[/\\\\]" OR includeLine MATCHES "include[ \t]*[<\"][^>\"]*\\.\\.")
      message(FATAL_ERROR "${centralSourceFile}: ${includeLine}: an include in core/ or plugins/ has no folder in a "
        "quoted name and no .. in any name")
    endif()
  endforeach()
endforeach()
get_property(centralTargets GLOBAL PROPERTY centralTargets)
foreach(centralTarget IN LISTS centralTargets)
  get_target_property(includeDirectories ${centralTarget} INCLUDE_DIRECTORIES)
  foreach(includeDirectory IN LISTS includeDirectories)
    cmake_path(SET normalizedDirectory NORMALIZE "${includeDirectory}")
    foreach(gamesFolder IN ITEMS "${repositoryDirectory}/games/" "${rootDirectory}/games/")
      string(FIND "${normalizedDirectory}/" "${gamesFolder}" gamesFolderPosition)
      if(gamesFolderPosition EQUAL 0)
        message(FATAL_ERROR "${centralTarget} is built from core/ or plugins/ and has an include directory under "
          "games/: ${includeDirectory}")
      endif()
    endforeach()
  endforeach()
endforeach()
