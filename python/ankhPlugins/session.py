"""Keeps one game open across commands, for iterating on the plugins.

  python -m ankhPlugins.session [--root <folder>] --game <name> [--gameFolder <folder>] <command> [arguments]

--game names a folder under the root's games; the root is this repository unless --root or the ankhPluginsRoot
environment variable names another. The install folder comes from --gameFolder or the ankhPluginsGameFolder environment
variable. Options go before the command: everything after it belongs to the command. launch records the process ID
and start time in ankh-plugins\\session.txt beside the game's executable; the other commands act on that game.
  launch [game arguments...]   start the game's executable without taking the foreground; waits for the loader
  status                       process, exit code, windows, foreground
  plugins                      the loaded plugins and which can be reloaded
  reload <dll>                 reload one plugin from the ankh-plugins folder while the game runs
  log <plugin> [INFO|WARN|ERROR] that plugin's session log for this game
  capture <png>                save the game's first visible window
  close                        ask the game's windows to close and wait
  kill                         terminate the game
"""
import argparse
import os
import sys

from ankhPlugins.gameProcess import GameError, GameProcess, waitFor
from ankhPlugins.games import Game, repositoryFolder
from ankhPlugins.linePipe import LoaderSession
from ankhPlugins.pluginLogs import sessionErrors, sessionLog
from ankhPlugins.windowCapture import captureWindow
from ankhPlugins.windowsApi import processIDsNamed

sessionFileName = "session.txt"
loaderPluginName = "loader"
loaderReadySeconds = 30
closeSeconds = 30


def recordedGame(game):
  path = game.pluginsFolder / sessionFileName
  if not path.is_file():
    raise GameError(f"no game was launched from {game.installFolder} ({path} is missing)")
  processID, startedAt = path.read_text(encoding="ascii").split()
  if int(processID) not in processIDsNamed(game.executableName):
    raise GameError(f"the game launched as process {processID} is no longer running")
  return GameProcess(game, int(processID), float(startedAt))


def loaderEvents(process):
  try:
    return [line.event for line in sessionLog(process.game.pluginsFolder, loaderPluginName, process.processID, process.startedAt)]
  except FileNotFoundError:
    return []


def describeErrors(errors):
  return "; ".join(f"{pluginName}: {line.message}" for pluginName, line in errors)


def waitForLoader(process):
  """Returns once the loader has loaded every listed plugin. A plugin's error or the game's exit raises GameError."""
  pluginsFolder = process.game.pluginsFolder
  waitFor(lambda: "loadComplete" in loaderEvents(process) or process.exitCode is not None
    or sessionErrors(pluginsFolder, process.processID, process.startedAt), loaderReadySeconds, "the loader's loadComplete")
  errors = sessionErrors(pluginsFolder, process.processID, process.startedAt)
  if errors:
    raise GameError(f"a plugin failed while loading (its message box is up in process {process.processID}): {describeErrors(errors)}")
  if process.exitCode is not None:
    raise GameError(f"the game exited with {process.exitCode} before the loader finished loading")


def launch(game, arguments):
  process = GameProcess.launch(game, arguments)
  (game.pluginsFolder / sessionFileName).write_text(f"{process.processID} {process.startedAt!r}\n", encoding="ascii")
  waitForLoader(process)
  print(f"processID={process.processID}")
  for line in sessionLog(game.pluginsFolder, loaderPluginName, process.processID, process.startedAt):
    print(line)


def status(game):
  process = recordedGame(game)
  print(f"processID={process.processID} exitCode={process.exitCode} foreground={process.isForeground()}")
  for window, title, rectangle in process.windows():
    print(f"window={window} title={title!r} rectangle={rectangle}")


def plugins(game):
  process = recordedGame(game)
  session = LoaderSession(process.processID)
  try:
    loaded, reloadable = session.plugins()
  finally:
    session.close()
  print(f"loaded={loaded} reloadable={reloadable}")


def reload(game, pluginFileName):
  process = recordedGame(game)
  session = LoaderSession(process.processID)
  try:
    print(f"generation={session.reload(pluginFileName)}")
  finally:
    session.close()


def log(game, pluginName, minimumLevel):
  process = recordedGame(game)
  for line in sessionLog(game.pluginsFolder, pluginName, process.processID, process.startedAt, minimumLevel):
    print(line)


def capture(game, picturePath):
  process = recordedGame(game)
  windows = process.windows()
  if not windows:
    raise GameError("the game has no visible window")
  captureWindow(windows[0][0]).save(picturePath)
  print(f"saved {picturePath} from window {windows[0][1]!r}")


def main():
  parser = argparse.ArgumentParser(prog="ankhPlugins.session", description=__doc__.split("\n")[0])
  parser.add_argument("--root", default=os.environ.get("ankhPluginsRoot", str(repositoryFolder)))
  parser.add_argument("--game", required=True)
  parser.add_argument("--gameFolder", default=os.environ.get("ankhPluginsGameFolder"))
  parser.add_argument("command", choices=["launch", "status", "plugins", "reload", "log", "capture", "close", "kill"])
  parser.add_argument("arguments", nargs=argparse.REMAINDER)
  options = parser.parse_args()
  if not options.gameFolder:
    parser.error("give --gameFolder or set ankhPluginsGameFolder")
  arguments = options.arguments
  try:
    game = Game(options.root, options.game, options.gameFolder)
  except ValueError as error:
    parser.error(str(error))
  try:
    if options.command == "launch":
      launch(game, arguments)
    elif options.command == "status":
      status(game)
    elif options.command == "plugins":
      plugins(game)
    elif options.command == "reload":
      if len(arguments) != 1:
        parser.error("reload takes one plugin DLL name")
      reload(game, arguments[0])
    elif options.command == "log":
      if len(arguments) not in (1, 2):
        parser.error("log takes a plugin name and an optional minimum level")
      log(game, arguments[0], arguments[1] if len(arguments) == 2 else "INFO")
    elif options.command == "capture":
      if len(arguments) != 1:
        parser.error("capture takes the output PNG path")
      capture(game, arguments[0])
    elif options.command == "close":
      print(f"exitCode={recordedGame(game).close(closeSeconds)}")
    elif options.command == "kill":
      print(f"exitCode={recordedGame(game).terminate(closeSeconds)}")
  except GameError as error:
    print(f"error: {error}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
