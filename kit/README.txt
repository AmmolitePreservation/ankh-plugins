Ankh plugins for any game
https://github.com/AmmolitePreservation/ankh-plugins

Each plugin is one DLL with one job, loaded into a game by the loader, which stands in for a system DLL the game
imports. The repository's README says what each plugin does. Nothing here changes the game's own files.

Install
1. Copy the ankh-plugins folder to beside the game's executable.
2. Copy one loader from the loaders folder to beside the executable: version.dll for most games, opengl32.dll for a
   game that imports that instead. A 32-bit game whose launcher should be skipped also takes msimg32.dll.
3. In ankh-plugins\game\game.cfg, set executable to the game's executable and loader to the loader you copied.
4. In ankh-plugins\game\plugins.txt, remove the # in front of each plugin the game should load.
5. Start the game.

After the first start
- ankh-plugins\plugins.txt is your own list, made from the one in the game folder: switch plugins on and off there.
- Each plugin writes its settings file, <plugin>.cfg, into ankh-plugins; its comments explain every setting.
- Each plugin logs to ankh-plugins\logs\<plugin>. The loader's log, logs\loader, lists what it loaded.
- A message box at the start names what is wrong: a game.cfg that names another executable or loader, a plugin that
  could not hook the game. Nothing fails silently.

Remove
Delete the loader DLL and the ankh-plugins folder.

Licenses are in ankh-plugins\licenses.
