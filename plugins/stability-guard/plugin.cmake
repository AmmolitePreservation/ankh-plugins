addPluginLibrary(${pluginName} "${pluginFolder}")
# dbghelp is loaded at runtime from the system folder, so it is not linked.
target_link_libraries(${pluginName} PRIVATE psapi)
