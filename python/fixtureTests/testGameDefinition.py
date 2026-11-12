"""The loader is the same for every game: which game it is in comes from the ankh-plugins\\game folder of the install,
the game.cfg and plugins.txt its package ships. Each test changes those in a copy of the fixture install that
build.ps1 -test makes and starts a program there. A loader that cannot use its game folder stops before it has a log,
so those tests read its message box."""
import os
import shutil
import time
import unittest

from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import GameError, waitFor
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionErrors, sessionLog, sessionLogPaths

copyFolderName = "fixtureInstallGameDefinition"
loaderSeconds = 30
exitSeconds = 10
# Far longer than the loader takes to open its log in a program it gives the plugins.
otherProgramSeconds = 3
otherLoader = {"version": "opengl32", "opengl32": "version"}


class GameDefinitionTest(unittest.TestCase):
  def setUp(self):
    installed = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    copyFolder = installed.installFolder.with_name(copyFolderName)
    if copyFolder.exists():
      shutil.rmtree(copyFolder)
    shutil.copytree(installed.installFolder, copyFolder, ignore=shutil.ignore_patterns("logs"))
    self.game = Game(installed.root, installed.name, copyFolder)
    self.gameFile = self.game.definitionFolder / "game.cfg"
    self.shippedList = self.game.definitionFolder / "plugins.txt"

  def start(self, executablePath):
    self.handle, self.processID = windowsApi.startProcess(executablePath, [], executablePath.parent)
    self.addCleanup(windowsApi.closeHandle, self.handle)
    self.addCleanup(self.terminate)
    self.startedAt = windowsApi.processCreationTime(self.handle)

  def terminate(self):
    if windowsApi.exitCode(self.handle) is None:
      windowsApi.terminateProcess(self.handle)
      if not windowsApi.waitForExit(self.handle, exitSeconds):
        raise GameError(f"process {self.processID} was still running {exitSeconds} seconds after being terminated")

  def loaderStop(self):
    """Starts the game and returns the message of the box the loader stops it with."""
    self.start(self.game.executablePath)
    waitFor(lambda: windowsApi.messageBoxes(self.processID) or windowsApi.exitCode(self.handle) is not None,
      loaderSeconds, "the loader's message box or the game's end")
    self.assertIsNone(windowsApi.exitCode(self.handle), "the game ended without a message box")
    ((title, message),) = windowsApi.messageBoxes(self.processID)
    self.assertEqual(title, "loader")
    self.assertEqual(sessionLogPaths(self.game.pluginsFolder, self.processID, self.startedAt), {})
    return message

  def rewriteGameFile(self, replace):
    lines = self.gameFile.read_text(encoding="ascii").splitlines()
    self.gameFile.write_text("\r\n".join(replace(lines)) + "\r\n", encoding="ascii", newline="")

  def testWithoutItsGameFileTheLoaderStopsAndNamesTheFile(self):
    self.gameFile.unlink()
    self.assertEqual(self.loaderStop(), f"Could not open {self.gameFile}: No such file or directory. The loader's "
      "package puts this file there; install it again.")

  def testAGameFileOfAGameWithAnotherLoaderStopsTheLoader(self):
    other = otherLoader[self.game.loader]
    self.rewriteGameFile(lambda lines: [f"loader={other}" if line.startswith("loader=") else line for line in lines])
    self.assertEqual(self.loaderStop(),
      f"{self.gameFile} is for a game whose loader is {other}.dll, but this loader is {self.game.loader}.dll")

  def testAGameFileOfAGameThatIsNotInTheFolderStopsTheLoader(self):
    self.rewriteGameFile(lambda lines: ["executable=anotherGame.exe" if line.startswith("executable=") else line
      for line in lines])
    self.assertEqual(self.loaderStop(),
      f"{self.gameFile} is for the game anotherGame.exe, which is not in {self.game.executablePath.parent}")

  def testALineThatIsNoSettingOfAGameStopsTheLoader(self):
    lineCount = len(self.gameFile.read_text(encoding="ascii").splitlines())
    self.rewriteGameFile(lambda lines: lines + ["publisher=nobody"])
    self.assertEqual(self.loaderStop(),
      f"{self.gameFile} line {lineCount + 1} is not a setting of a game: publisher=nobody")

  def testAProgramTheGameFileDoesNotNameRunsWithoutThePlugins(self):
    otherProgram = self.game.executablePath.with_name("otherProgram.exe")
    shutil.copyfile(self.game.executablePath, otherProgram)
    self.start(otherProgram)
    time.sleep(otherProgramSeconds)
    self.assertIsNone(windowsApi.exitCode(self.handle))
    self.assertEqual(windowsApi.messageBoxes(self.processID), [])
    self.assertEqual(sessionLogPaths(self.game.pluginsFolder, self.processID, self.startedAt), {})

  def testAPluginTheGameFolderListsIsAddedToThePlayersList(self):
    playersList = self.game.pluginsFolder / "plugins.txt"
    with self.shippedList.open("a", encoding="ascii", newline="") as shipped:
      shipped.write("\r\n#shippedLater.dll\r\n")
    self.start(self.game.executablePath)
    waitFor(lambda: "loadComplete" in self.loaderEvents() or self.errors() or windowsApi.exitCode(self.handle) is not None,
      loaderSeconds, "the loader's loadComplete, an error or the game's end")
    self.assertEqual(self.errors(), [])
    added = [line.fields for line in self.loaderLog() if line.event == "pluginAdded"]
    self.assertIn({"file": "plugins.txt", "plugin": "shippedLater.dll", "on": "false"}, added)
    self.assertIn("#shippedLater.dll", playersList.read_text(encoding="ascii").splitlines())

  def loaderLog(self):
    if "loader" not in sessionLogPaths(self.game.pluginsFolder, self.processID, self.startedAt):
      return []
    return sessionLog(self.game.pluginsFolder, "loader", self.processID, self.startedAt)

  def loaderEvents(self):
    return [line.event for line in self.loaderLog()]

  def errors(self):
    return sessionErrors(self.game.pluginsFolder, self.processID, self.startedAt)
