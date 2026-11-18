"""stability-guard and a game's own crash filter, in the fixture game's crash mode (crashMode.cpp): the game installs its
filter, writes crashing and faults. The x64 fixture's handler withholds fatal crashes from the game's filter; the x86
fixture's does not."""
import os
import unittest

from ankhPlugins.gameProcess import GameProcess, waitFor
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionLog

reportFileName = "fixtureReport.txt"
pluginFileName = "stability-guard.dll"
exitSeconds = 60
accessViolation = 0xC0000005


class StabilityGuardTest(unittest.TestCase):
  def setUp(self):
    self.game = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    if not (self.game.pluginsFolder / pluginFileName).is_file():
      self.skipTest(f"{self.game.name} has no {pluginFileName} installed")
    self.reportPath = self.game.executablePath.parent / reportFileName
    self.reportPath.unlink(missing_ok=True)

  def reportEvents(self):
    if not self.reportPath.is_file():
      return []
    return [line.split(" ")[0] for line in self.reportPath.read_text(encoding="ascii").splitlines()]

  def crash(self):
    process = GameProcess.launch(self.game, ["crash"])
    self.addCleanup(process.terminate, exitSeconds)
    waitFor(lambda: process.exitCode is not None, exitSeconds, "the fixture game's end after its crash")
    events = [line.event for line in sessionLog(self.game.pluginsFolder, "stability-guard", process.processID, process.startedAt)]
    return process.exitCode & 0xFFFFFFFF, events

  def testAFatalCrashIsReportedAndEndsTheGame(self):
    exitCode, events = self.crash()
    self.assertEqual(exitCode, accessViolation)
    self.assertEqual(events.count("crashFatal"), 1)
    self.assertIn("crashing", self.reportEvents())

  def testTheGameFilterRunsOnlyWhereTheHandlerDoesNotWithholdIt(self):
    _, events = self.crash()
    isWithheld = "gameFilterWithholding" in events
    self.assertEqual(isWithheld, self.game.name == "fixture-x64")
    self.assertEqual("gameFilterWithheld" in events, isWithheld)
    self.assertEqual("gameFilterRan" in self.reportEvents(), not isWithheld)


if __name__ == "__main__":
  unittest.main()
