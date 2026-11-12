cmake_minimum_required(VERSION 3.25)
project(ankhPlugins C CXX ASM_MASM)

# One build tree per root and architecture holds every game of that architecture. What no game changes is built once
# in it: the shared code as a library, each loader, each central plugin's own code, and the DLL of a plugin a game
# takes as it is. A game adds only what is its own: its plugins, and its build of a central plugin it gives a handler
# or settings. Each game is a target named after it, and its files land in games/<game>/bin.
if(NOT MSVC)
  message(FATAL_ERROR "ankh-plugins builds Windows DLLs with MSVC; configure it from a Visual Studio environment (build.ps1 does)")
endif()
if(buildArchitecture STREQUAL "x64")
  set(expectedPointerSize 8)
elseif(buildArchitecture STREQUAL "x86")
  set(expectedPointerSize 4)
else()
  message(FATAL_ERROR "buildArchitecture must be x64 or x86 (build.ps1 sets it from the game's game.cfg)")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL expectedPointerSize)
  message(FATAL_ERROR "the compiler builds for a different architecture than ${buildArchitecture}; configure from the Visual Studio ${buildArchitecture} environment (build.ps1 does)")
endif()
if(NOT DEFINED rootDirectory)
  message(FATAL_ERROR "rootDirectory is not set (build.ps1 sets it from -root)")
endif()
if(NOT DEFINED diagnosticPlugins)
  message(FATAL_ERROR "diagnosticPlugins is not set (build.ps1 sets it from layout.ps1)")
endif()
if(NOT DEFINED ankhPluginsBuildCommit)
  message(FATAL_ERROR "ankhPluginsBuildCommit is not set (build.ps1 sets it from git)")
endif()
# Real paths, so a root that is this repository under another spelling of its path is still this repository.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" repositoryDirectory)
file(REAL_PATH "${rootDirectory}" rootDirectory)

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
set(centralBuildDirectory "${CMAKE_BINARY_DIR}/central")
set(generatedDirectory "${CMAKE_BINARY_DIR}/generated")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(coreDirectory "${repositoryDirectory}/core")
set(minhookDirectory "${coreDirectory}/shared/vendor/minhook")
if(buildArchitecture STREQUAL "x64")
  set(minhookDisassembler "hde64.c")
else()
  set(minhookDisassembler "hde32.c")
endif()
add_library(minhook STATIC EXCLUDE_FROM_ALL
  "${minhookDirectory}/src/buffer.c"
  "${minhookDirectory}/src/hook.c"
  "${minhookDirectory}/src/trampoline.c"
  "${minhookDirectory}/src/hde/${minhookDisassembler}")
target_include_directories(minhook PUBLIC "${minhookDirectory}/include" PRIVATE "${minhookDirectory}/src")
target_compile_options(minhook PRIVATE /O2 /Gy /Brepro)

function(applyCompileSettings targetName)
  # MSVC deprecates the bounded C string functions the code uses in favor of its _s variants.
  target_compile_definitions(${targetName} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS
    "ankhPluginsBuildCommit=\"${ankhPluginsBuildCommit}\"")
  target_compile_options(${targetName} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /EHs-c- /GR- /Gy /utf-8 /O2 /Brepro>)
endfunction()

function(applyLinkSettings targetName)
  # /Brepro leaves no timestamps, so the same source builds the same bytes and a release only moves when its code does.
  target_link_options(${targetName} PRIVATE /OPT:REF /OPT:ICF /Brepro)
endfunction()

# Code under core/ and a plugins/ beside games/ is central: registerCentralTarget notes a target built from it alone,
# and the end of this file checks that none of them can reach into games/.
function(registerCentralTarget targetName)
  set_property(GLOBAL APPEND PROPERTY centralTargets ${targetName})
endfunction()

# What every DLL is linked with. Function-level linking lets the linker drop what a DLL does not use.
file(GLOB sharedSources CONFIGURE_DEPENDS "${coreDirectory}/shared/source/*.cpp")
add_library(shared STATIC EXCLUDE_FROM_ALL ${sharedSources})
target_include_directories(shared PUBLIC "${coreDirectory}/shared/source")
target_link_libraries(shared PUBLIC minhook)
applyCompileSettings(shared)
registerCentralTarget(shared)

# The PluginInterfaceVersion export the loader checks before it calls a plugin's Load.
file(GLOB pluginInterfaceSources CONFIGURE_DEPENDS "${coreDirectory}/plugin/source/*.cpp")
add_library(pluginInterface OBJECT EXCLUDE_FROM_ALL ${pluginInterfaceSources})
target_link_libraries(pluginInterface PRIVATE shared)
applyCompileSettings(pluginInterface)
registerCentralTarget(pluginInterface)

# embedDefaultFiles(<targetName> <settings file>...): the library carries its settings files, embedded at build
# time, and writes one out when the ankh-plugins folder lacks it, so a release ships no settings files.
function(embedDefaultFiles targetName)
  set(outputPath "${generatedDirectory}/${targetName}DefaultFiles.cpp")
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

# addLibrary(<targetName> <outputName> <buildDirectory> [SOURCES <source>...] [SETTINGS <settings file>...]
# [LIBRARIES <library>...]): <outputName>.dll in <buildDirectory>/bin, built only when a game takes it.
function(addLibrary targetName outputName buildDirectory)
  cmake_parse_arguments(PARSE_ARGV 3 library "" "" "SOURCES;SETTINGS;LIBRARIES")
  add_library(${targetName} SHARED EXCLUDE_FROM_ALL ${library_SOURCES})
  # Import libraries go elsewhere: a loader's has the name of a Windows SDK one and would shadow it in a link directory.
  set_target_properties(${targetName} PROPERTIES OUTPUT_NAME ${outputName}
    RUNTIME_OUTPUT_DIRECTORY "${buildDirectory}/bin" ARCHIVE_OUTPUT_DIRECTORY "${buildDirectory}/lib")
  target_link_libraries(${targetName} PRIVATE ${library_LIBRARIES} shared)
  applyCompileSettings(${targetName})
  applyLinkSettings(${targetName})
  embedDefaultFiles(${targetName} ${library_SETTINGS})
endfunction()

# addCentralLibrary(<targetName> <outputName> <folder> [SOURCES <extra source>...]): a DLL that is no plugin, from
# <folder>/source, the same for every game.
function(addCentralLibrary targetName outputName folder)
  cmake_parse_arguments(PARSE_ARGV 3 central "" "" "SOURCES")
  file(GLOB centralSources CONFIGURE_DEPENDS "${folder}/source/*.cpp")
  addLibrary(${targetName} ${outputName} "${centralBuildDirectory}" SOURCES ${centralSources} ${central_SOURCES})
  registerCentralTarget(${targetName})
endfunction()

# pluginSettingsFiles(<pluginFolder> <gameDirectory>): the plugin's .cfg files into settingsFiles, each replaced by the
# game's own copy of it (games/<game>/<file>.cfg) when the game has one; those copies into gameSettingsFiles. An empty
# <gameDirectory> is no game.
function(pluginSettingsFiles pluginFolder gameDirectory)
  file(GLOB ownSettingsFiles CONFIGURE_DEPENDS "${pluginFolder}/*.cfg")
  set(settingsFiles "")
  set(gameSettingsFiles "")
  foreach(settingsFile IN LISTS ownSettingsFiles)
    get_filename_component(settingsFileName "${settingsFile}" NAME)
    if(gameDirectory AND EXISTS "${gameDirectory}/${settingsFileName}")
      list(APPEND settingsFiles "${gameDirectory}/${settingsFileName}")
      list(APPEND gameSettingsFiles "${gameDirectory}/${settingsFileName}")
    else()
      list(APPEND settingsFiles "${settingsFile}")
    endif()
  endforeach()
  set(settingsFiles "${settingsFiles}" PARENT_SCOPE)
  set(gameSettingsFiles "${gameSettingsFiles}" PARENT_SCOPE)
endfunction()

# pluginHandlers(<pluginName> <pluginFolder> <gameDirectory>): <pluginFolder>/handlers/<handler>.cpp is the part of the
# plugin a game may replace: when games/<game>/handlers/<pluginName>/<handler>.cpp exists it is compiled in its place,
# against the plugin's headers. A game that replaces a handler may give it more: the .cpp files of
# games/<game>/handlers/<pluginName>/source are compiled with it, and
# games/<game>/handlers/<pluginName>/<pluginName>.<part>.cfg are settings files of its own. The sources go into
# handlerSources, the game's among them into gameHandlerSources, and the settings files into handlerSettingsFiles.
function(pluginHandlers pluginName pluginFolder gameDirectory)
  file(GLOB defaultHandlers CONFIGURE_DEPENDS "${pluginFolder}/handlers/*.cpp")
  set(gameHandlers "")
  set(gameHandlerParts "")
  set(gameHandlerSettings "")
  if(gameDirectory)
    set(gameHandlerFolder "${gameDirectory}/handlers/${pluginName}")
    file(GLOB gameHandlers CONFIGURE_DEPENDS "${gameHandlerFolder}/*.cpp")
    file(GLOB gameHandlerParts CONFIGURE_DEPENDS "${gameHandlerFolder}/source/*.cpp")
    file(GLOB gameHandlerSettings CONFIGURE_DEPENDS "${gameHandlerFolder}/*.cfg")
  endif()
  set(handlerNames "")
  set(handlerSources "")
  foreach(defaultHandler IN LISTS defaultHandlers)
    get_filename_component(handlerName "${defaultHandler}" NAME)
    list(APPEND handlerNames "${handlerName}")
    if(gameDirectory AND EXISTS "${gameHandlerFolder}/${handlerName}")
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
  set(handlerSources ${handlerSources} ${gameHandlerParts} PARENT_SCOPE)
  set(gameHandlerSources ${gameHandlers} ${gameHandlerParts} PARENT_SCOPE)
  set(handlerSettingsFiles "${gameHandlerSettings}" PARENT_SCOPE)
endfunction()

# addPluginLibrary(<pluginName> <pluginFolder> [SOURCES <extra source>...] [SETTINGS <extra settings file>...]): a
# plugin the loader loads, <pluginName>.dll. It sets pluginTarget, the target a plugin.cmake gives what the plugin's
# code needs to compile and link.
# A central plugin (pluginGame is empty) is the code every game shares, compiled once: pluginTarget is an object
# library, and what a plugin.cmake links it with reaches every DLL built from it. Its DLLs come later, when a game
# lists the plugin: one for all the games that take it as it is, and one for each game with a part of its own in it.
# A game's own plugin (games/<game>/plugins/<pluginName>, pluginGame is the game) is its DLL at once, and pluginTarget
# is that DLL, <game>.<pluginName>.
function(addPluginLibrary pluginName pluginFolder)
  cmake_parse_arguments(PARSE_ARGV 2 plugin "" "" "SOURCES;SETTINGS")
  file(GLOB pluginSources CONFIGURE_DEPENDS "${pluginFolder}/source/*.cpp")
  if(pluginGame)
    set(gameDirectory "${rootDirectory}/games/${pluginGame}")
    pluginHandlers(${pluginName} "${pluginFolder}" "${gameDirectory}")
    pluginSettingsFiles("${pluginFolder}" "${gameDirectory}")
    set(pluginTarget "${pluginGame}.${pluginName}")
    addLibrary(${pluginTarget} ${pluginName} "${CMAKE_BINARY_DIR}/games/${pluginGame}"
      SOURCES ${pluginSources} ${handlerSources} ${plugin_SOURCES}
      SETTINGS ${settingsFiles} ${handlerSettingsFiles} ${plugin_SETTINGS}
      LIBRARIES pluginInterface)
    target_include_directories(${pluginTarget} PRIVATE "${pluginFolder}/source")
    set_property(GLOBAL APPEND PROPERTY "ownPlugins.${pluginGame}" ${pluginName})
    set_property(GLOBAL APPEND PROPERTY "usedSettingsFiles.${pluginGame}" ${gameSettingsFiles})
  else()
    set(pluginTarget ${pluginName})
    add_library(${pluginTarget} OBJECT EXCLUDE_FROM_ALL ${pluginSources} ${plugin_SOURCES})
    target_include_directories(${pluginTarget} PUBLIC "${pluginFolder}/source")
    target_link_libraries(${pluginTarget} PUBLIC shared)
    applyCompileSettings(${pluginTarget})
    registerCentralTarget(${pluginTarget})
    set_property(GLOBAL APPEND PROPERTY centralPluginNames ${pluginName})
    set_property(GLOBAL PROPERTY "pluginFolder.${pluginName}" "${pluginFolder}")
    set_property(GLOBAL PROPERTY "pluginSettings.${pluginName}" "${plugin_SETTINGS}")
  endif()
  set(pluginTarget ${pluginTarget} PARENT_SCOPE)
endfunction()

# addPluginFolders(<parentFolder> <game>): every folder in it is a plugin named after the folder, central for an empty
# <game> and that game's own otherwise. One that needs more than addPluginLibrary gives carries a plugin.cmake, which
# is included in its place and sees pluginName, pluginFolder and pluginGame.
function(addPluginFolders parentFolder pluginGame)
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

# requireLoader(<loaderName>): the loader as the system DLL <loaderName>.dll, which a game imports (game.cfg loader=),
# the same for every game that imports that DLL: which game it is in comes from the install (ankh-plugins\game).
# core/loader/proxies/<loaderName>.txt names the DLL's exports, one per line; the export table, the macro
# systemProxy.cpp expands and x64's stubs (it has no inline assembly; x86's are in systemProxy.cpp) are generated from
# it. Everything else of the loader is compiled once for all of them.
file(GLOB loaderSources CONFIGURE_DEPENDS "${coreDirectory}/loader/source/*.cpp")
list(REMOVE_ITEM loaderSources "${coreDirectory}/loader/source/systemProxy.cpp")
add_library(loaderCore OBJECT EXCLUDE_FROM_ALL ${loaderSources})
target_link_libraries(loaderCore PRIVATE shared)
applyCompileSettings(loaderCore)
registerCentralTarget(loaderCore)
function(requireLoader loaderName)
  if(TARGET loader.${loaderName})
    return()
  endif()
  set(proxyListFile "${coreDirectory}/loader/proxies/${loaderName}.txt")
  if(NOT EXISTS "${proxyListFile}")
    message(FATAL_ERROR "core/loader/proxies has no ${loaderName}.txt, which a game's loader=${loaderName} needs")
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
  set(proxyDirectory "${generatedDirectory}/loader.${loaderName}")
  file(CONFIGURE OUTPUT "${proxyDirectory}/proxyExports.h" @ONLY CONTENT
    "#pragma once\n\n#define proxyLibraryName \"${loaderName}.dll\"\n\n#define proxyExports(X) \\\n${proxyMacroBody}\n")
  file(CONFIGURE OUTPUT "${proxyDirectory}/proxyExports.def" @ONLY CONTENT
    "LIBRARY ${loaderName}.dll\nEXPORTS\n${proxyDefinitionBody}\n")
  set(proxyStubs "")
  if(buildArchitecture STREQUAL "x64")
    set(proxyStubs "${proxyDirectory}/proxyStubs.asm")
    # A single jump, so the caller's arguments and return address reach the real function untouched.
    file(CONFIGURE OUTPUT "${proxyStubs}" @ONLY CONTENT
      "OPTION CASEMAP:NONE\n${proxyExternBody}\n\n.CODE\n${proxyStubBody}\n\nEND\n")
  endif()
  addLibrary(loader.${loaderName} ${loaderName} "${centralBuildDirectory}"
    SOURCES "${coreDirectory}/loader/source/systemProxy.cpp" "${proxyDirectory}/proxyExports.def" ${proxyStubs}
    LIBRARIES loaderCore)
  target_include_directories(loader.${loaderName} PRIVATE "${proxyDirectory}")
  registerCentralTarget(loader.${loaderName})
endfunction()

# requireCentralPlugin(<pluginName>): the DLL of a central plugin for every game that has no part of its own in it.
function(requireCentralPlugin pluginName)
  if(TARGET central.${pluginName})
    return()
  endif()
  get_property(pluginFolder GLOBAL PROPERTY "pluginFolder.${pluginName}")
  get_property(extraSettingsFiles GLOBAL PROPERTY "pluginSettings.${pluginName}")
  pluginHandlers(${pluginName} "${pluginFolder}" "")
  pluginSettingsFiles("${pluginFolder}" "")
  addLibrary(central.${pluginName} ${pluginName} "${centralBuildDirectory}"
    SOURCES ${handlerSources} SETTINGS ${settingsFiles} ${extraSettingsFiles} LIBRARIES ${pluginName} pluginInterface)
  registerCentralTarget(central.${pluginName})
endfunction()

# The central plugins are this repository's plugins/ and, for a root of its own (build.ps1 -root), that root's
# plugins/ as well.
set(centralPluginFolders "${repositoryDirectory}/plugins" "${rootDirectory}/plugins")
list(REMOVE_DUPLICATES centralPluginFolders)
foreach(centralPluginFolder IN LISTS centralPluginFolders)
  addPluginFolders("${centralPluginFolder}" "")
endforeach()
get_property(centralPluginNames GLOBAL PROPERTY centralPluginNames)

# gameSettingValues(<variable> <key> <fewest> <most>): every value the game's game.cfg gives the key, in the file's order.
function(gameSettingValues variable key fewest most)
  set(values "")
  foreach(line IN LISTS gameConfigLines)
    if(line MATCHES "^[ \t]*${key}[ \t]*=[ \t]*(.*[^ \t])[ \t]*$")
      list(APPEND values "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(LENGTH values valueCount)
  if(valueCount LESS fewest OR valueCount GREATER most)
    message(FATAL_ERROR "${gameDirectory}/game.cfg sets ${key} ${valueCount} times; it takes ${fewest} to ${most}")
  endif()
  set(${variable} "${values}" PARENT_SCOPE)
endfunction()

# stageGameFile(<sourcePath> <stagedName> [<target>]): a copy of the file in the game's bin under <stagedName>, which
# build.ps1 -gameFolder, a release and a package take it from. It is added to stagedFiles.
function(stageGameFile sourcePath stagedName)
  set(stagedPath "${gameBinaryDirectory}/${stagedName}")
  add_custom_command(OUTPUT "${stagedPath}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${sourcePath}" "${stagedPath}"
    DEPENDS "${sourcePath}" ${ARGN}
    VERBATIM)
  set(stagedFiles ${stagedFiles} "${stagedPath}" PARENT_SCOPE)
endfunction()

# addGame(<game>): the target <game>, which builds everything the game's install takes into games/<game>/bin: the
# loader its game.cfg names, every plugin its plugins.txt lists and the diagnostic plugins, each the game's own build
# where the game has a part in it and a copy of the central one otherwise, and in game/ the game.cfg and plugins.txt
# the loader reads there. A function, so that what one game's game.cmake sets does not reach the next game.
function(addGame game)
  set(gameDirectory "${rootDirectory}/games/${game}")
  set(gameBuildDirectory "${CMAKE_BINARY_DIR}/games/${game}")
  set(gameBinaryDirectory "${gameBuildDirectory}/bin")
  if(game STREQUAL "central")
    message(FATAL_ERROR "${gameDirectory}: a game cannot be named central, which names what no game changes")
  endif()
  gameSettingValues(gameExecutable executable 1 1)
  gameSettingValues(gameLoader loader 1 1)
  gameSettingValues(gameLauncher launcher 0 1)
  gameSettingValues(gameTimeDateStamp timeDateStamp 0 1)

  if(NOT EXISTS "${gameDirectory}/plugins.txt")
    message(FATAL_ERROR "${gameDirectory}/plugins.txt does not exist")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${gameDirectory}/plugins.txt")
  file(STRINGS "${gameDirectory}/plugins.txt" pluginListLines)
  set(gamePlugins "")
  foreach(line IN LISTS pluginListLines)
    if(line MATCHES "^[ \t]*#?[ \t]*([^\\\\/: \t#]+)\\.dll[ \t]*$")
      list(APPEND gamePlugins "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(APPEND gamePlugins ${diagnosticPlugins})
  list(REMOVE_DUPLICATES gamePlugins)

  addPluginFolders("${gameDirectory}/plugins" "${game}")
  get_property(ownPlugins GLOBAL PROPERTY "ownPlugins.${game}")
  set(gameTargets "")
  foreach(ownPlugin IN LISTS ownPlugins)
    list(APPEND gameTargets "${game}.${ownPlugin}")
  endforeach()

  set(stagedFiles "")
  set(usedSettingsFiles "")
  foreach(pluginName IN LISTS gamePlugins)
    if(pluginName IN_LIST ownPlugins)
      continue()
    endif()
    if(NOT pluginName IN_LIST centralPluginNames)
      message(FATAL_ERROR "${game} takes ${pluginName}.dll (its plugins.txt or the diagnostic plugins), and no plugins folder holds ${pluginName}")
    endif()
    get_property(pluginFolder GLOBAL PROPERTY "pluginFolder.${pluginName}")
    get_property(extraSettingsFiles GLOBAL PROPERTY "pluginSettings.${pluginName}")
    pluginHandlers(${pluginName} "${pluginFolder}" "${gameDirectory}")
    pluginSettingsFiles("${pluginFolder}" "${gameDirectory}")
    list(APPEND usedSettingsFiles ${gameSettingsFiles})
    if(gameHandlerSources OR gameSettingsFiles)
      addLibrary(${game}.${pluginName} ${pluginName} "${gameBuildDirectory}"
        SOURCES ${handlerSources} SETTINGS ${settingsFiles} ${handlerSettingsFiles} ${extraSettingsFiles}
        LIBRARIES ${pluginName} pluginInterface)
      list(APPEND gameTargets "${game}.${pluginName}")
    else()
      requireCentralPlugin(${pluginName})
      stageGameFile("$<TARGET_FILE:central.${pluginName}>" "${pluginName}.dll" central.${pluginName})
    endif()
  endforeach()

  requireLoader(${gameLoader})
  stageGameFile("$<TARGET_FILE:loader.${gameLoader}>" "${gameLoader}.dll" loader.${gameLoader})
  if(gameLauncher)
    if(NOT TARGET launcher-skip)
      message(FATAL_ERROR "${gameDirectory}/game.cfg names a launcher, and launcher-skip is not built for ${buildArchitecture}")
    endif()
    stageGameFile("$<TARGET_FILE:launcher-skip>" "msimg32.dll" launcher-skip)
  endif()
  # The two files the loader reads, with CRLF line endings whatever the checkout's are, so the same commit ships the
  # same bytes.
  foreach(gameFileName IN ITEMS game.cfg plugins.txt)
    set(stagedPath "${gameBinaryDirectory}/game/${gameFileName}")
    add_custom_command(OUTPUT "${stagedPath}"
      COMMAND "${CMAKE_COMMAND}" "-DsourcePath=${gameDirectory}/${gameFileName}" "-DoutputPath=${stagedPath}"
        -P "${repositoryDirectory}/cmake/copyTextFile.cmake"
      DEPENDS "${gameDirectory}/${gameFileName}" "${repositoryDirectory}/cmake/copyTextFile.cmake"
      VERBATIM)
    list(APPEND stagedFiles "${stagedPath}")
  endforeach()
  add_custom_target(${game} ALL DEPENDS ${stagedFiles})
  if(gameTargets)
    add_dependencies(${game} ${gameTargets})
  endif()

  # A folder or settings file of the game that no plugin it takes answers to would be left out without a word.
  file(GLOB handlerFolders LIST_DIRECTORIES true "${gameDirectory}/handlers/*")
  foreach(handlerFolder IN LISTS handlerFolders)
    get_filename_component(handlerFolderName "${handlerFolder}" NAME)
    if(NOT handlerFolderName IN_LIST gamePlugins)
      message(FATAL_ERROR "${handlerFolder} names no plugin ${game} takes (its plugins.txt or the diagnostic plugins)")
    endif()
  endforeach()
  get_property(ownUsedSettingsFiles GLOBAL PROPERTY "usedSettingsFiles.${game}")
  list(APPEND usedSettingsFiles ${ownUsedSettingsFiles} "${gameDirectory}/game.cfg")
  file(GLOB gameFolderSettingsFiles CONFIGURE_DEPENDS "${gameDirectory}/*.cfg")
  foreach(gameFolderSettingsFile IN LISTS gameFolderSettingsFiles)
    if(NOT gameFolderSettingsFile IN_LIST usedSettingsFiles)
      message(FATAL_ERROR "${gameFolderSettingsFile} replaces no settings file of a plugin ${game} takes")
    endif()
  endforeach()

  # What a game builds beyond plugins. It sees game, gameDirectory, gameBinaryDirectory and the game.cfg values above,
  # reaches the game's build of a plugin as ${game}.<plugin>, and adds what it builds to the target ${game}.
  if(EXISTS "${gameDirectory}/game.cmake")
    include("${gameDirectory}/game.cmake")
  endif()
endfunction()

file(GLOB gameConfigFiles CONFIGURE_DEPENDS "${rootDirectory}/games/*/game.cfg")
if(NOT gameConfigFiles)
  message(FATAL_ERROR "${rootDirectory}/games holds no game (a folder with a game.cfg)")
endif()
foreach(gameConfigFile IN LISTS gameConfigFiles)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${gameConfigFile}")
  get_filename_component(gameDirectory "${gameConfigFile}" DIRECTORY)
  get_filename_component(game "${gameDirectory}" NAME)
  file(STRINGS "${gameConfigFile}" gameConfigLines)
  gameSettingValues(gameArchitecture architecture 1 1)
  if(NOT gameArchitecture MATCHES "^(x64|x86)$")
    message(FATAL_ERROR "${gameConfigFile}: architecture must be x64 or x86, not ${gameArchitecture}")
  endif()
  if(gameArchitecture STREQUAL buildArchitecture)
    addGame(${game})
  endif()
endforeach()

# Code under core/ and a plugins/ beside games/ cannot reach into games/: its quoted includes name a file beside it or
# in an include directory, none climbs out of a folder, and no central target has an include directory under games/,
# this repository's or the root's.
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
  if(NOT includeDirectories)
    continue()
  endif()
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
