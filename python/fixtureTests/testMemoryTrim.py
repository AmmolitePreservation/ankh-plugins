"""Runs memory-trim in the fixture game acting as a Direct3D 9 game, which goes on pumping messages once its report is
ready. It never has the foreground, so the background interval is the one that trims it."""
import time

from ankhPlugins.gameProcess import waitFor
from direct3DFixture import Direct3DFixtureTest

windowed = ["windowed", "500", "400"]
trimSeconds = 1
waitSeconds = 10
quietSeconds = 3


class MemoryTrimTest(Direct3DFixtureTest):
  def launchTrimming(self, background, foreground):
    self.launch(windowed, {"memory-trim": {"backgroundTrimSeconds": background, "foregroundTrimSeconds": foreground}})

  def trims(self):
    return self.logged("memory-trim", "workingSetTrimmed")

  def testABackgroundGameIsTrimmedAtTheBackgroundInterval(self):
    self.launchTrimming(trimSeconds, 0)
    self.assertFalse(self.process.isForeground())
    self.assertEqual(self.logged("memory-trim", "memoryTrimEnabled"),
      [{"backgroundTrimSeconds": str(trimSeconds), "foregroundTrimSeconds": "0"}])
    waitFor(lambda: len(self.trims()) >= 2, waitSeconds, "two trims")
    for trim in self.trims():
      with self.subTest(trim=trim):
        self.assertEqual(trim["foreground"], "0")
        self.assertLess(int(trim["afterKB"]), int(trim["beforeKB"]) // 2)

  def testABackgroundGameIsNotTrimmedAtTheForegroundInterval(self):
    self.launchTrimming(0, trimSeconds)
    self.assertFalse(self.process.isForeground())
    time.sleep(quietSeconds)
    self.assertEqual(self.trims(), [])

  def testNoIntervalsTurnTrimmingOff(self):
    self.launchTrimming(0, 0)
    events = [line.event for line in self.log("memory-trim")]
    self.assertEqual(events.count("memoryTrimOff"), 1)
    self.assertNotIn("memoryTrimEnabled", events)
