"""Runs the loader's check of the game's build (game.cfg's timeDateStamp) in a copy of the fixture install that
build.ps1 -test makes, whose executable is given another timeDateStamp. Every test starts its own program."""
import os
import shutil
import struct
import unittest

from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import GameError, waitFor
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionErrors, sessionLog, sessionLogPaths

loaderSeconds = 30
exitSeconds = 10
otherBuildFolderName = "fixtureInstallOtherBuild"
otherTimeDateStamp = 0x5A5A5A5A
loadedPluginLogs = ["database-dao", "display-guard", "display-report", "echo", "frame-control", "loader", "memory-trim",
  "stability-guard"]


def setTimeDateStamp(executablePath, timeDateStamp):
  image = bytearray(executablePath.read_bytes())
  (headerOffset,) = struct.unpack_from("<I", image, 0x3C)
  struct.pack_into("<I", image, headerOffset + 8, timeDateStamp)
  executablePath.write_bytes(image)


class BuildCheckTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    installed = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    otherBuildFolder = installed.installFolder.with_name(otherBuildFolderName)
    if otherBuildFolder.exists():
      shutil.rmtree(otherBuildFolder)
    shutil.copytree(installed.installFolder, otherBuildFolder, ignore=shutil.ignore_patterns("logs"))
    cls.game = Game(installed.root, installed.name, otherBuildFolder)
    setTimeDateStamp(cls.game.executablePath, otherTimeDateStamp)

  def start(self, executablePath):
    """Starts the program and waits until its loader has loaded the plugins, logged an error or ended."""
    self.handle, self.processID = windowsApi.startProcess(executablePath, [], executablePath.parent)
    self.addCleanup(windowsApi.closeHandle, self.handle)
    self.addCleanup(self.terminate)
    self.startedAt = windowsApi.processCreationTime(self.handle)
    waitFor(lambda: "loadComplete" in self.loaderEvents() or self.errors() or windowsApi.exitCode(self.handle) is not None,
      loaderSeconds, "the loader's loadComplete, an error or the program's end")

  def terminate(self):
    if windowsApi.exitCode(self.handle) is None:
      windowsApi.terminateProcess(self.handle)
      if not windowsApi.waitForExit(self.handle, exitSeconds):
        raise GameError(f"process {self.processID} was still running {exitSeconds} seconds after being terminated")

  def logPaths(self):
    return sessionLogPaths(self.game.pluginsFolder, self.processID, self.startedAt)

  def loaderLog(self):
    if "loader" not in self.logPaths():
      return []
    return sessionLog(self.game.pluginsFolder, "loader", self.processID, self.startedAt)

  def loaderEvents(self):
    return [line.event for line in self.loaderLog()]

  def errors(self):
    return sessionErrors(self.game.pluginsFolder, self.processID, self.startedAt)

  def loggedTimeDateStamp(self):
    (sessionStart,) = [line for line in self.loaderLog() if line.event == "sessionStart"]
    return sessionStart.fields["timeDateStamp"]

  def assertThePluginsLoadedInTheOtherBuild(self):
    self.assertEqual(self.errors(), [])
    self.assertEqual(self.loaderEvents().count("loadComplete"), 1)
    self.assertEqual(sorted(self.logPaths()), loadedPluginLogs)
    self.assertEqual(self.loggedTimeDateStamp(), f"0x{otherTimeDateStamp:08X}")

  def testAnotherBuildOfTheGameStopsTheLoaderBeforeAnyPlugin(self):
    if self.game.timeDateStamp is None:
      self.skipTest(f"{self.game.name} names no build in its game.cfg")
    self.start(self.game.executablePath)
    self.assertEqual([(pluginName, line.message) for pluginName, line in self.errors()], [("loader",
      "This is not the build of the game these plugins were made for: the executable's timeDateStamp is "
      f"0x{otherTimeDateStamp:08X}, not 0x{self.game.timeDateStamp:08X}")])
    self.assertEqual(sorted(self.logPaths()), ["loader"])
    self.assertNotIn("loadComplete", self.loaderEvents())
    self.assertEqual(self.loggedTimeDateStamp(), f"0x{otherTimeDateStamp:08X}")

  def testAnyBuildOfTheGameGetsThePluginsWhenGameCfgNamesNone(self):
    if self.game.timeDateStamp is not None:
      self.skipTest(f"{self.game.name} names a build in its game.cfg")
    self.start(self.game.executablePath)
    self.assertThePluginsLoadedInTheOtherBuild()

  def testACompanionOfAnotherBuildGetsThePlugins(self):
    (companionPath,) = self.game.companionPaths
    shutil.copyfile(self.game.executablePath, companionPath)
    self.start(companionPath)
    self.assertThePluginsLoadedInTheOtherBuild()
