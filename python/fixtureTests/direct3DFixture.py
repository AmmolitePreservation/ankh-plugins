"""The fixture game run as a Direct3D 9 game (its direct3DMode.cpp), in a copy of the install that build.ps1 -test
makes, so a test can give the plugins its own settings. The game writes what it asked for and what it got to
fixtureReport.txt beside its executable, one event per line, and ends the list with ready."""
import os
import shutil
import unittest

from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import GameProcess, waitFor
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionErrors, sessionLog
from ankhPlugins.session import waitForLoader

reportFileName = "fixtureReport.txt"
gameWindowTitle = "fixtureGame"
copyFolderName = "fixtureInstallDirect3D"
readySeconds = 30
exitSeconds = 10


class ReportLine:
  def __init__(self, rawLine):
    tokens = rawLine.split(" ")
    self.event = tokens[0]
    self.fields = dict(token.split("=", 1) for token in tokens[1:])

  def __repr__(self):
    return f"{self.event} {self.fields}"


def size(fields, widthName="width", heightName="height"):
  return (int(fields[widthName]), int(fields[heightName]))


class Direct3DFixtureTest(unittest.TestCase):
  def launch(self, arguments, settings, isActivated=False):
    """Starts the game with "direct3D" and these arguments, each plugin's settings file holding the given values, and
    returns once its report ends with ready."""
    installed = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    copyFolder = installed.installFolder.with_name(copyFolderName)
    if copyFolder.exists():
      shutil.rmtree(copyFolder)
    shutil.copytree(installed.installFolder, copyFolder, ignore=shutil.ignore_patterns("logs", reportFileName))
    self.game = Game(installed.root, installed.name, copyFolder)
    for pluginName, values in settings.items():
      lines = "".join(f"{key}={value}\n" for key, value in values.items())
      (self.game.pluginsFolder / f"{pluginName}.cfg").write_text(lines, encoding="ascii")
    self.process = GameProcess.launch(self.game, ["direct3D", *arguments], isActivated)
    self.addCleanup(self.process.terminate, exitSeconds)
    waitForLoader(self.process)
    waitFor(lambda: {"ready", "failed"} & set(self.reportEvents()) or self.process.exitCode is not None, readySeconds,
      "the fixture game's ready")
    if "ready" not in self.reportEvents():
      self.fail(f"the fixture game did not get ready (exit code {self.process.exitCode}): {self.report()}")
    self.assertEqual(sessionErrors(self.game.pluginsFolder, self.process.processID, self.process.startedAt), [])

  def report(self):
    path = self.game.executablePath.parent / reportFileName
    if not path.is_file():
      return []
    return [ReportLine(rawLine) for rawLine in path.read_text(encoding="ascii").splitlines()]

  def reportEvents(self):
    return [line.event for line in self.report()]

  def reported(self, event):
    """The fields of every report line of this event, in order."""
    return [line.fields for line in self.report() if line.event == event]

  def log(self, pluginName):
    return sessionLog(self.game.pluginsFolder, pluginName, self.process.processID, self.process.startedAt)

  def logged(self, pluginName, event):
    """The fields of every line of this event in the plugin's log, in order."""
    return [line.fields for line in self.log(pluginName) if line.event == event]

  def gameWindow(self):
    (window,) = [window for window in windowsApi.topLevelWindows(self.process.processID)
      if windowsApi.windowTitle(window) == gameWindowTitle]
    return window
