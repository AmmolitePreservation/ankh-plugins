"""watchModuleLoads in the fixture game run in its module mode (moduleMode.cpp): the game loads fixtureModule.dll,
calls its export and frees it, twice, and module-probe hooks the export at each load and takes the hook back at each
unload."""
import os
import unittest

from ankhPlugins.gameProcess import GameProcess, waitFor
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionErrors, sessionLog

reportFileName = "fixtureReport.txt"
pluginFileName = "module-probe.dll"
readySeconds = 30
exitSeconds = 10
hookedTarget = "fixtureModule!fixtureModuleValue"


class ModuleWatchTest(unittest.TestCase):
  def setUp(self):
    self.game = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    pluginLines = (self.game.definitionFolder / "plugins.txt").read_text(encoding="ascii").splitlines()
    if pluginFileName not in pluginLines:
      self.skipTest(f"{self.game.name} does not load {pluginFileName}")
    self.reportPath = self.game.executablePath.parent / reportFileName
    self.reportPath.unlink(missing_ok=True)

  def report(self):
    if not self.reportPath.is_file():
      return []
    lines = []
    for rawLine in self.reportPath.read_text(encoding="ascii").splitlines():
      event, *tokens = rawLine.split(" ")
      lines.append((event, dict(token.split("=", 1) for token in tokens)))
    return lines

  def testAModuleFreedAndLoadedAgainIsHookedAtEachLoadAndUnhookedAtEachUnload(self):
    self.process = GameProcess.launch(self.game, ["module"])
    self.addCleanup(self.process.terminate, exitSeconds)
    waitFor(lambda: {"ready", "failed"} & {event for event, _ in self.report()} or self.process.exitCode is not None,
      readySeconds, "the end of the fixture game's report")
    self.assertEqual(self.report(), [
      ("moduleValue", {"cycle": "1", "value": "7359"}),
      ("moduleFreed", {"cycle": "1", "isLoaded": "0"}),
      ("moduleValue", {"cycle": "2", "value": "7359"}),
      ("moduleFreed", {"cycle": "2", "isLoaded": "0"}),
      ("ready", {}),
    ])
    probeLog = sessionLog(self.game.pluginsFolder, "module-probe", self.process.processID, self.process.startedAt)
    self.assertEqual([(line.event, line.fields) for line in probeLog if line.event.startswith("hook")], [
      ("hookInstalled", {"target": hookedTarget}),
      ("hookRemoved", {"target": hookedTarget}),
      ("hookInstalled", {"target": hookedTarget}),
      ("hookRemoved", {"target": hookedTarget}),
    ])
    self.assertEqual(sessionErrors(self.game.pluginsFolder, self.process.processID, self.process.startedAt), [])


if __name__ == "__main__":
  unittest.main()
