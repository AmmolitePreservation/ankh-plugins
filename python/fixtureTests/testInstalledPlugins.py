"""A listed plugin is loaded when it is installed: the loader skips one whose DLL is not in the ankh-plugins folder,
as when a player has not installed that plugin's package. Run in a copy of the fixture install with one DLL removed."""
import os
import shutil
import unittest

from ankhPlugins.gameProcess import GameProcess
from ankhPlugins.games import Game
from ankhPlugins.linePipe import LoaderSession
from ankhPlugins.pluginLogs import sessionErrors, sessionLog
from ankhPlugins.session import waitForLoader

copyFolderName = "fixtureInstallOnePluginLess"
absentPlugin = "frame-control.dll"
exitSeconds = 10


class InstalledPluginsTest(unittest.TestCase):
  def setUp(self):
    installed = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    copyFolder = installed.installFolder.with_name(copyFolderName)
    if copyFolder.exists():
      shutil.rmtree(copyFolder)
    shutil.copytree(installed.installFolder, copyFolder, ignore=shutil.ignore_patterns("logs"))
    self.game = Game(installed.root, installed.name, copyFolder)
    (self.game.pluginsFolder / absentPlugin).unlink()
    self.process = GameProcess.launch(self.game, [])
    self.addCleanup(self.process.terminate, exitSeconds)
    waitForLoader(self.process)

  def testAListedPluginThatIsNotInstalledIsSkippedAndTheRestLoad(self):
    session = LoaderSession(self.process.processID)
    self.addCleanup(session.close)
    loaded, _ = session.plugins()
    self.assertEqual(sorted(loaded), ["database-dao.dll", "display-guard.dll", "display-report.dll", "echo.dll",
      "stability-guard.dll"])
    loaderLog = sessionLog(self.game.pluginsFolder, "loader", self.process.processID, self.process.startedAt)
    self.assertEqual([line.fields for line in loaderLog if line.event == "pluginNotInstalled"], [{"plugin": absentPlugin}])
    self.assertEqual(sessionErrors(self.game.pluginsFolder, self.process.processID, self.process.startedAt), [])

  def testThePluginKeepsItsLineInTheList(self):
    listed = (self.game.pluginsFolder / "plugins.txt").read_text(encoding="ascii").split()
    self.assertIn(absentPlugin, listed)
