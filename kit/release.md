# Ankh plugins for any game
Every plugin of this repository and the loader, built for 32-bit and 64-bit Windows games, for a game that has no download of its own here. Each plugin is one DLL with one job; the repository's README says what each does and how to find out what a game needs.

## Install
1. Take the zip that matches the game: `x86` for a 32-bit game, `x64` for a 64-bit one.
2. Copy the `ankh-plugins` folder to beside the game's executable.
3. Copy one loader from the `loaders` folder to beside the executable: `version.dll` for most games, `opengl32.dll` for a game that imports that instead.
4. In `ankh-plugins\game\game.cfg`, set `executable` to the game's executable and `loader` to the loader you copied.
5. In `ankh-plugins\game\plugins.txt`, remove the `#` in front of each plugin the game should load.

The game's own files are not changed. `README.txt` in the zip has the same steps and what to look at when a plugin does not load.

## Remove
Delete the loader DLL and the `ankh-plugins` folder.
