# Balatro borderless window fix
Stops Balatro's borderless window from blanking the screen each time the game gains or loses focus. Balatro draws with OpenGL through SDL2, and on an HDR monitor with an NVIDIA card the driver treats a window that exactly covers the monitor as fullscreen and switches the monitor out of HDR and back. This release shows Balatro's frames the way Direct3D games are shown, through Windows' own desktop composition, so the driver has no OpenGL window to take over. The game runs as a borderless window covering the monitor whatever display mode it is set to.

## Install
1. Extract the zip into the folder that holds `Balatro.exe`. In Steam: right-click Balatro, Manage, Browse local files.
2. Play.

It adds `version.dll` beside `Balatro.exe` and an `ankh-plugins` folder, and changes none of the game's files.

## Remove
Delete `version.dll` and the `ankh-plugins` folder.
