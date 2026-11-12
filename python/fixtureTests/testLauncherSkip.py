"""launcher-skip (msimg32.dll) in the fixture game whose game.cfg names a launcher: the build makes that launcher,
which imports msimg32.dll and would end with its own exit code if nothing skipped it."""
import os
import unittest

from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import GameProcess
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionErrors, sessionLog
from ankhPlugins.session import waitForLoader

exitSeconds = 10


class LauncherSkipTest(unittest.TestCase):
  def setUp(self):
    self.game = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    if self.game.launcherPath is None:
      self.skipTest(f"{self.game.name} names no launcher in its game.cfg")

  def testTheLauncherStartsTheGameInItsPlaceAndEnds(self):
    self.assertEqual(windowsApi.processIDsNamed(self.game.executableName), [])
    launcherHandle, launcherProcessID = windowsApi.startProcess(self.game.launcherPath, [], self.game.launcherPath.parent)
    self.addCleanup(windowsApi.closeHandle, launcherHandle)
    launcherStartedAt = windowsApi.processCreationTime(launcherHandle)
    self.assertTrue(windowsApi.waitForExit(launcherHandle, exitSeconds))
    (gameProcessID,) = windowsApi.processIDsNamed(self.game.executableName)
    process = GameProcess(self.game, gameProcessID, None)
    self.addCleanup(process.terminate, exitSeconds)
    self.assertEqual(windowsApi.exitCode(launcherHandle), 0)
    waitForLoader(process)
    skipLog = sessionLog(self.game.pluginsFolder, "launcher-skip", launcherProcessID, launcherStartedAt)
    self.assertEqual([line.event for line in skipLog], ["sessionStart", "gameStarted", "launcherEnded"])
    (gameStarted,) = [line.fields for line in skipLog if line.event == "gameStarted"]
    self.assertEqual(gameStarted, {"processID": str(gameProcessID), "path": str(self.game.executablePath)})
    self.assertEqual(sessionErrors(self.game.pluginsFolder, launcherProcessID, launcherStartedAt), [])
