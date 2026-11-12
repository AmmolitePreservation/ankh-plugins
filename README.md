# ankh-plugins
Plugins for the [Ankh](https://github.com/AmmolitePreservation/ankh) launcher's Windows games, 64-bit and 32-bit: the loader, the source every plugin shares, and the plugins that need no knowledge of an online game. Each plugin is a standalone DLL with one job, loaded into the game by the loader, which stands in for a system DLL the game imports (`version.dll` for most), with its settings and logs in an `ankh-plugins` folder beside the game's executable. The game's own files are never modified. No installer, no registry.

The plugin is the unit: it is written and built once, for every game, and a game takes it by naming it in its `plugins.txt`. The loader is the same for every game too: which game it is in comes from two small files of the install. Game code exists only where a game needs something no central plugin can do, and only that is built for the game.

## Install
A game's release is a zip to extract into the folder that holds the game's executable; it needs nothing else, Ankh included. Its notes say what it fixes and anything the game needs set. To remove it, delete the loader DLL its notes name (`version.dll` for most, and `msimg32.dll` for a game whose release has one) and the `ankh-plugins` folder from the game's folder. Each plugin writes its settings file into `ankh-plugins` the first time the game starts.

Releases are listed at [releases](https://github.com/AmmolitePreservation/ankh-plugins/releases). Publishing is switched off (see Releases), so a game is installed from a build for now (see Build, test and install).

## Layout
| Folder | What it holds |
|---|---|
| `core/loader/` | The loader, built once as each system DLL a game.cfg's `loader=` names (`version.dll`, `opengl32.dll`): it has every export of the system copy, each passing its call on. `proxies/<name>.txt` lists a DLL's exports, and the build generates the export table and the stubs from it. Windows picks the loader up from the executable's folder when the game or one of its DLLs imports that DLL. No game is built into it: it reads `ankh-plugins\game\game.cfg` of the install to learn which game it is in, and stops with a message when that file is missing, is another game's or is for another loader. It acts only in the game's executable and its companions (game.cfg); in any other program in the folder it only passes calls on. On the main thread and before any game code runs, it brings `plugins.txt` up to the release's plugins (see Settings files), then loads each DLL named there from a shadow copy and calls its `Load` export: at the game's entry point when Windows loads it at startup, or, when a packed executable loads it while unpacking itself, at that thread's first `LoadLibrary`, `GetProcAddress` or C runtime startup call outside the loader lock (`startMode` in its log). While the game runs it answers one-line commands on `\\.\pipe\ankh-plugins-<process ID>`: `plugins`, `reload <dll>` for a plugin that exports `Unload`, and every command a plugin registers. Its own log is `logs\loader`. |
| `core/shared/` | Source every DLL is linked with, compiled once: the host interface (`pluginHost.h`), session log, fatal error, hook registry (inline hooks, import table hooks, code patches), method table and Direct3D 9 hooks, a watch for a DLL the game loads later, command pipe, settings files, the install's game folder, and vendored MinHook. |
| `core/plugin/` | Source every plugin is linked with and the loader is not: the `PluginInterfaceVersion` export. |
| `plugins/<name>/` | One plugin, built once for all the games that take it as it is. |
| `games/<game>/` | One game: `game.cfg`, `plugins.txt` (the plugins its release ships), `release.md` (its release notes, starting with a `#` title), `<plugin>.cfg` for any plugin whose shipped settings differ for this game, `handlers/<plugin>/` for the handlers of a plugin this game replaces (see Writing a plugin), and `plugins/<name>/` for plugins only this game needs. |
| `python/ankhPlugins/` | A game from outside: launches it without taking the foreground, talks to the loader's pipe, reads the plugins' session logs, and captures the game's window. |
| `fixtureRoot/` | The loader's test fixture, laid out as a root of its own (see below): a small game the build makes itself, which can also act as a Direct3D 9 game, the three fixture games that run it, and `echo`, a plugin whose commands exercise every way the loader answers one. |
| `python/tests/`, `python/fixtureTests/` | The Python client's tests, and the tests that run the loader and the plugins in the fixture game. |

Code under `core/` and `plugins/` names no game and includes nothing from `games/`, which the build checks. Plugin, game, DLL and settings file names are kebab-case; names inside the code are camelCase.

A root is a folder that holds `games/` and, optionally, `plugins/`. This repository is one. `build.ps1 -root <folder>` builds a game of another: a repository that carries this one as a submodule and adds games and plugins of its own, or `fixtureRoot/` here. The game then comes from that root's `games/`; its plugins are this repository's `plugins/`, the root's `plugins/` and the game's own; and the build lands in the root's `build\<architecture>`. The rule for central code holds for a root's `plugins/` too, and two plugins of one name stop the build.

## Plugins
| Plugin | What it does |
|---|---|
| `stability-guard` | Diagnostic, not in releases. Crash and freeze evidence: a crash the game may survive gets a report (registers, unwound frames, stack candidates, modules) and a small memory dump; a crash that ends the game, or a window that stops responding for `freezeSeconds`, gets a dump of all the game's memory; and every way the process ends itself is logged with its exit code and stack. Dumps sit in `logs\stability-guard`, the oldest removed past `maxDumpGigabytes`. Also makes quitting always work: closing the window arms a timer that ends a game stuck on its way out, and Ctrl+Alt+F4 ends the game in front at once. Its `gameFixes.cpp` handler is where a game puts the fixes for faults in its own code, installed after the guard's own hooks; without one there are none. Settings in `stability-guard.cfg`. |
| `display-report` | Diagnostic, not in releases. Reports what the game does with its display, without changing it: every monitor and its DPI, the process's DPI awareness, every top-level window the main thread creates and each later change of its style or placement, every display mode change, and which graphics APIs the game loads at runtime. Around focus: activation, focus, size and close messages; mouse confinement, capture, visibility, cursor warps, raw input registrations and Windows hooks, each with the module that asked; SDL2's grab, relative mouse, minimize, fullscreen and hint calls when the game uses SDL2; and frame pacing at OpenGL's `SwapBuffers` and Direct3D 9's `Present` (stalls, frames per second for 10 seconds after each activation change, a summary each minute). Every change of any monitor's desktop mode, link refresh rate or HDR state, sampled four times a second, and every display mode change request in the process at the system call, whoever made it. What to fix comes from these logs. |
| `display-guard` | Makes a Direct3D 9 game a good neighbor on the desktop. Its window, the one its device presents into, becomes a borderless window covering `monitor`, whatever the game asks for: fullscreen or windowed, and through every later restyle, move and reset. It never changes the display's mode. `renderResolution` is `native` (the game renders at the monitor's size) or `game` (it renders at the size it asked for, scaled to the monitor with its shape kept and black bars around it); either way the game sees a window exactly the size of its back buffer, mouse positions included. With `releaseInput`, a game in the background cannot take the foreground, move or confine the cursor or read the keys, and its DirectInput 8 devices are non-exclusive. `startMinimized` shows it minimized first. A game replaces where the picture goes with its own `presentationPolicy.cpp` handler. Settings in `display-guard.cfg`. |
| `frame-control` | Holds a Direct3D 9 game's frame rate at its presents: `foregroundFramesPerSecond` while one of its windows is the active one, `backgroundFramesPerSecond` while another application's is. 0 leaves it unlimited. Settings in `frame-control.cfg`. |
| `d3d9-borderless` | Turns a Direct3D 9 game's exclusive fullscreen into a borderless window at the desktop's own mode: the device is created and reset windowed, the device window becomes a popup over its monitor (the game still decides when it is shown), and the game's resolution list offers only the desktop mode. `frameRateLimit` holds the frame rate at `IDirect3DDevice9::Present`: `auto` (the monitor's refresh rate), `0` (none) or a number. |
| `borderless-fit` | Sizes the game's borderless window when it asks for exactly its monitor's rectangle (popup style, main thread, at creation and on each resize): `heightAdjustment` is taller (one pixel past the bottom edge), shorter (one pixel short), or none. A window that matches its monitor exactly is taken over by some drivers as if it were fullscreen. |
| `launcher-skip` | For a game whose Steam entry starts a launcher (game.cfg `launcher=`): as `msimg32.dll`, which the launcher imports, it starts the game at the launcher's entry point and ends the launcher. Which launcher and which game it reads from `ankh-plugins\game\game.cfg`, as the loader does. In any other program it passes calls on to the system copy. The loader does not load it, so it is not listed in `plugins.txt`. x86 only so far. |

## Games
| Game | Folder | Status |
|---|---|---|
| Jade Empire: Special Edition (Steam 7110, `JadeEmpire.exe` behind Steam's DRM wrapper, x86, Direct3D 9) | `games/jade-empire` | Fixed in the main menu and character creation; gameplay and combat not yet played. Its own plugins: `steam-locator` loads Steam's own `steam.dll` (the game looks for it only beside itself); `config-bypass`, with `launcher-skip`, skips the configuration utility and the launcher; `render-settings` writes the desktop mode, widescreen and `ClampFPS=0` into `JadeEmpire.ini`; `intro-hold` presents nothing before the first intro movie; `time-fixes` computes the engine clock in double precision (Direct3D 9's single precision x87 rounded it to steps of 65.5 ms after 6.4 days of uptime, so the world updated about 15 times a second) and puts physics (120 fixed steps a second), life textures, mesh scrolling and wind on game time. `d3d9-borderless` runs it borderless at the refresh rate. Each Steam launch still asks for administrator rights: Steam's install script sets that on every launch. |
| SOMA (Steam 282140, `Soma.exe` 1.110, HPL3 on SDL2 and OpenGL) | `games/soma` | Fixed by `borderless-fit` (taller). The NVIDIA driver took the monitor over whenever SOMA's OpenGL window exactly covered it and was in front, switching an HDR monitor to SDR and back on each focus change, which blanked it for 2 to 3 seconds; the driver's OpenGL present method made no difference. Open: no sound in the background, since the game sleeps while out of focus (`SleepWhenOutOfFocus`). |
| The loader's test fixture | `fixtureRoot/games/fixture-x64`, `fixtureRoot/games/fixture-x86`, `fixtureRoot/games/fixture-x86-opengl32` | Not a release: `fixtureGame.exe`, built here, with the `echo` plugin. `build.ps1 -root fixtureRoot -game <game> -test` runs the loader in it: as `version.dll` for each architecture, and as `opengl32.dll` on x86. `fixture-x64` replaces `echo`'s handler with its own and `fixture-x86-opengl32` `display-guard`'s; `fixture-x86` loads Direct3D 9 and DirectInput 8 after it starts instead of importing them, and has a launcher the build makes too, for `launcher-skip`. |

## Game folder layout
```
<install folder>\
  <executable>            the game's executable, unmodified (game.cfg names it)
  version.dll             the loader, named as game.cfg's loader= says; must sit beside the executable
  msimg32.dll             launcher-skip, for a game with a launcher
  ankh-plugins\
    game\game.cfg         which game this is, for the loader: the game's game.cfg, replaced by each release
    game\plugins.txt      the release's plugins in load order: the game's plugins.txt, replaced by each release
    plugins.txt           the plugins to load, in order: the player's, started from the release's and kept up with it
    <plugin>.dll, <plugin>.cfg
    loaded\               shadow copies the game runs from, so a build can replace any plugin
    logs\<plugin>\        <plugin>_<date>_<time>_pid<process ID>.log, kept 90 days; stability-guard's .dmp files are kept by total size instead
```

## Build, test and install
On Windows with Visual Studio 2022 and its C++ x86 and x64 build tools (MSVC, ml64, CMake, Ninja). The Python client runs in the repository's virtual environment, made once with Python 3.14:

```powershell
python -m venv .venv
.venv\Scripts\python.exe -m pip install -e .
```

A build is for one game:

```powershell
.\build.ps1 -game soma -test -diagnostics -gameFolder "<SOMA install folder>"
```

Run it as `powershell -NoProfile -File build.ps1 ...` when calling from another PowerShell, which otherwise turns native stderr into errors.

The games of one architecture (game.cfg) share a build tree, `build\<architecture>` under their root (see Layout), and each game is a target in it. What no game changes is built once there, in `central\bin`, however many games are built: the shared code as a library, the loader as each system DLL, each plugin's own code, and the DLL of a plugin for every game that takes it as it is. Only what is a game's own is built for the game: its plugins, and its build of a central plugin it gives a handler or a settings file. So a second game of the same architecture compiles little, and one with no code of its own compiles nothing. A game's files land in `build\<architecture>\games\<game>\bin`, checked for the exports the loader and Windows look up: the loader its `game.cfg` names, the plugins its `plugins.txt` lists and the diagnostic ones (its own builds, and copies of the central DLLs), and in `game\` its `game.cfg` and `plugins.txt` as an install carries them. A plugin no game lists is not built.

`-test` runs the Python client's tests and, for a fixture game (`.\build.ps1 -root fixtureRoot -game fixture-x64 -test`), installs the build beside the game the build made and runs the loader's tests in it (`python/fixtureTests`): plugin loading, a call passed on to the system DLL, the command pipe, pump callbacks on the game's main thread, a game's handler in place of a plugin's, reload, a companion, the check of the game's build, what the loader does with a game folder it cannot use, and `launcher-skip` in the launcher the build makes for `fixture-x86`. Started with `direct3D windowed <width> <height>` or `direct3D fullscreen`, the fixture game creates a window, a Direct3D 9 device and a DirectInput 8 keyboard as a game does, and writes what it asked for and what it got to `fixtureReport.txt`; the tests of `display-guard` and `frame-control` read that report, the plugins' logs and the window itself. They need a graphics adapter Direct3D 9 can create a hardware device on, and the fixture's window covers the primary monitor for a few seconds a test. The fixture games are the only ones the tests start; a real game is checked by playing it.

`-gameFolder` lays the build out in that install folder as a release does (`layout.ps1`): the loader DLL beside the executable and the rest in `ankh-plugins`, the game's `game.cfg` and `plugins.txt` in its `game` folder. It copies no settings files: the plugins write and update their own (see Settings files). `-diagnostics` also installs `stability-guard` and `display-report` and lists them in `plugins.txt`; the loader keeps a listed plugin the release does not ship as long as its DLL is in the folder. Removing the loader DLL from the install folder turns every plugin off.

`.\package.ps1 -game soma -outputPath <zip>` packs a built game's release: the same layout, without the diagnostic plugins.

### Releases
A game is released only when its release files change. `release.ps1` builds every game under `games` with `-buildCommit unversioned`, so the DLLs depend only on the code; builds are reproducible (`/Brepro`). It then fingerprints the files the game's zip ships, and compares that with the fingerprint in the notes of the game's newest release. A game whose fingerprint differs is built again with the commit stamped in, packed, and, with `-publish`, released as `<game>-<yyyy.MM.dd>-<commit>`, with its `release.md` as title and notes. A change to a plugin the game does not ship, or to anything outside the code, leaves its release alone. A game with no `release.md` is built and tested but never released. With `-test`, `release.ps1` first builds and tests the fixture games under `fixtureRoot/games`.

`.github/workflows/release.yml` runs `release.ps1 -test` on each pull request to `main` and each push to `main`: every game and fixture game is built, the tests run, and the games that would be released are reported. Nothing is published: the workflow does not pass `-publish`.

### game.cfg
- `executable=` the game's executable, relative to its install folder; the loader DLL and `ankh-plugins` go beside it.
- `architecture=` `x64` or `x86`, which picks the build.
- `loader=` the system DLL the game imports and the loader stands in for: a name with an export list in `core/loader/proxies` (`version`, `opengl32`). It has to be a DLL Windows takes from the game's folder: one on Windows' known-DLLs list, or one already in the process before the game's imports are resolved, never comes from there.
- `companion=` (any number) another program beside the executable that also gets the plugins, such as one the launcher runs before the game.
- `launcher=` (at most one) the launcher the game's Steam entry starts, which `launcher-skip` replaces with the game.
- `timeDateStamp=` (at most one) the build of the executable the game's handlers and addresses were written for: its timeDateStamp as the loader's log prints it at `sessionStart` (`0x` and eight hexadecimal digits in capitals). In any other build of the game the loader stops with a message before it loads a plugin; a companion is not checked. Without it the plugins load in any build.

### Settings files
Each plugin carries its settings files, embedded at build time, the one copy of them: each plugin's `.cfg` from its folder, or the game's own copy in `games/<game>/`, which replaces it in that game's build of the plugin. The game's `plugins.txt` ships as a file, `ankh-plugins\game\plugins.txt`, beside the `game.cfg` the loader reads. A file missing from `ankh-plugins` is written with the shipped defaults when the game starts, so deleting one starts it over. An existing file is used as it is, and one from an earlier release is brought up to date by key: a setting it lacks is appended with its default and the comment above it in the shipped file (`settingAdded` in the plugin's log), and a key the plugin does not read, such as one a newer release dropped, or a line that is not key=value, is removed from the file and logged with its text (`settingRemoved`). The player's `plugins.txt` starts as a copy of the shipped one and is kept the same way: a plugin a newer release adds is inserted with its shipped state, after the plugin that precedes it in the release's list (`pluginAdded`), and a line naming a plugin the release does not ship and the folder does not hold is removed (`pluginRemoved`). The list is the order the game's plugins load in, not what is installed: each plugin can be a package of its own, and a listed plugin whose DLL is not in the folder is skipped (`pluginNotInstalled`).

## Writing a plugin
A plugin is a folder holding `source/*.cpp` and its `<name>.cfg` settings files: under `plugins/` (this repository's or a root's) when any game could use it, under `games/<game>/plugins/` when only that game needs it. The build picks the folder up on its own and names the DLL after it. A plugin that needs more than that (a library to link, another output name) carries a `plugin.cmake`, which the build includes in place of its default, as `stability-guard` and `launcher-skip` do: after `addPluginLibrary` it gives `${pluginTarget}` what the plugin's code needs, which for a central plugin reaches every DLL built from it. A game's `game.cmake` is for what the game builds beyond plugins; it names the game's build of a plugin `${game}.<plugin>` and adds its own targets to the target `${game}`.

A plugin's `handlers/*.cpp` are the parts of it a game may replace. Each defines functions that a header in the plugin's `source/` declares, with the behavior that fits any game. A game that needs its own puts a file of the same name in `games/<game>/handlers/<plugin>/`, and the build compiles that file in place of the plugin's, against the plugin's headers, into that game's build of the plugin; the plugin's own code is not compiled again for it. A game's handler may be more than that one file: the `.cpp` files in `games/<game>/handlers/<plugin>/source/` are compiled with it, and it includes their headers as `"source/<name>.h"`; and it may have settings of its own in `games/<game>/handlers/<plugin>/<plugin>.<part>.cfg`, shipped and completed like the plugin's settings files and read with `loadSettingsFile`. Either of those without a replaced handler stops the build. A game file that matches no handler, a folder there that names no plugin the game takes, or a `.cfg` in the game's folder that replaces no settings file of one, stops the build. This is the only way game code gets into a central plugin: an include under `core/` or `plugins/` names a file beside it or in an include directory (no `..`, no folder in a quoted name), and none of their include directories is under `games/`; the build checks both each time it is configured.

The DLL exports three functions (`core/shared/source/pluginHost.h`):

- `PluginInterfaceVersion`, compiled in from `core/plugin`. The loader reads it before anything else and refuses a plugin built against another version of the host interface, naming both versions.
- `Load(const PluginHost*)`, which the loader calls on the game's main thread before any game code runs. The host gives it the `ankh-plugins` folder, the session's log stamp, the main thread's ID, pump callbacks (`addPumpCallback`, `removePumpCallback`: run on the game's main thread each time it pumps window messages) and `registerCommand` for commands on the loader's pipe.
- `Unload`, optional, which makes the plugin reloadable while the game runs: it removes everything the plugin installed and returns once none of its code can run again.

A plugin builds on `core/shared` for its session log, settings, fatal errors, hooks and code patches. Nothing in a plugin falls back: a missing setting, a hook that cannot be installed or bytes that are not the expected ones end the game with a message that says what failed. A plugin that changes what the game gets from a Windows function hooks the game executable's import of it (`installImportHook`), not the function: only the game's own calls reach the hook, never the plugin's, another plugin's or a system library's, so plugins can hook the same function in any order. A function the executable does not import is logged as absent; calls the game makes from a DLL of its own, or through `GetProcAddress`, are not covered. A plugin that patches a packed executable's code waits for the code to be unpacked, with a pump callback (the game's first message pump), and checks the bytes it patches before it does.

## Adding a game
Report first, then fix: the diagnostic plugins record what the game does, and a fix is written against what their logs show.

A game that needs nothing of its own is two files, `game.cfg` and `plugins.txt`: nothing is compiled for it, and its install is the loader and the central plugins as they are already built.

1. `games/<game>/game.cfg` (see game.cfg). The executable, or a DLL it loads, must load the DLL `loader=` names, at startup or while it unpacks itself. A DLL the loader has not stood in for before needs its export list in `core/loader/proxies` first: the names `dumpbin /exports` shows for the system copy, one per line.
2. `games/<game>/plugins.txt` and `games/<game>/release.md`.
3. Build with `-diagnostics -gameFolder <install folder>`, reproduce the problem, and read `ankh-plugins\logs\display-report` and `stability-guard` (see Iterating).
4. Take the fix from `plugins/` when one is there; otherwise write it (see Writing a plugin).
5. List the plugin in the game's `plugins.txt`, confirm the fix in play, and add the game to the Games table.

A game whose handlers or plugins hold addresses or code bytes of its executable names that build in game.cfg (`timeDateStamp=`): the loader's log (`ankh-plugins\logs\loader`) prints the running executable's at `sessionStart`.

## Iterating
`ankhPlugins.session` keeps one game open across commands. It takes the game from `--game` (a game of another root with `--root` or the `ankhPluginsRoot` environment variable), the install folder from `--gameFolder` or the `ankhPluginsGameFolder` environment variable, and records the launched process in `ankh-plugins\session.txt`:

```powershell
$env:ankhPluginsGameFolder = "<SOMA install folder>"
.venv\Scripts\python.exe -m ankhPlugins.session --game soma launch
.venv\Scripts\python.exe -m ankhPlugins.session --game soma plugins
.venv\Scripts\python.exe -m ankhPlugins.session --game soma log display-report
.venv\Scripts\python.exe -m ankhPlugins.session --game soma reload <plugin>.dll
.venv\Scripts\python.exe -m ankhPlugins.session --game soma capture shot.png
.venv\Scripts\python.exe -m ankhPlugins.session --game soma close
```

`launch` passes its extra arguments to the game and refuses while the game's executable is already running. `status` shows the process and its windows, and `kill` terminates a game that will not close.
