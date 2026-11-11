"""Runs frame-control in the fixture game acting as a Direct3D 9 game. The game presents ten frames through its
device and ten through its swap chain, as fast as it can, and reports how long each batch took; it never has the
foreground, so the background rate is the one that paces it."""
from direct3DFixture import Direct3DFixtureTest

windowed = ["windowed", "500", "400"]
batches = ("devicePresents", "swapChainPresents")
# Ten frames are nine intervals; the first frame of a batch is not held back by the batch before it.
intervalCount = 9
timerSlackMilliseconds = 5


def batchMilliseconds(framesPerSecond):
  return intervalCount * 1000 // framesPerSecond


class FrameControlTest(Direct3DFixtureTest):
  def launchPaced(self, foreground, background):
    self.launch(windowed, {"frame-control": {"foregroundFramesPerSecond": foreground, "backgroundFramesPerSecond": background}})

  def milliseconds(self, batch):
    (reported,) = self.reported(batch)
    self.assertEqual(reported["count"], "10")
    return int(reported["milliseconds"])

  def testTheBackgroundRatePacesBothWaysOfPresenting(self):
    self.launchPaced(0, 20)
    self.assertFalse(self.process.isForeground())
    self.assertEqual(self.logged("frame-control", "framePacingEnabled"), [{"foregroundFPS": "0", "backgroundFPS": "20"}])
    for batch in batches:
      with self.subTest(batch=batch):
        self.assertGreaterEqual(self.milliseconds(batch), batchMilliseconds(20) - timerSlackMilliseconds)

  def testABackgroundGameIsNotPacedByTheForegroundRate(self):
    self.launchPaced(5, 20)
    for batch in batches:
      with self.subTest(batch=batch):
        self.assertGreaterEqual(self.milliseconds(batch), batchMilliseconds(20) - timerSlackMilliseconds)
        self.assertLess(self.milliseconds(batch), batchMilliseconds(5))

  def testNoBackgroundRateLeavesABackgroundGameUnpaced(self):
    self.launchPaced(20, 0)
    self.assertEqual(self.logged("frame-control", "framePacingEnabled"), [{"foregroundFPS": "20", "backgroundFPS": "0"}])
    for batch in batches:
      with self.subTest(batch=batch):
        self.assertLess(self.milliseconds(batch), batchMilliseconds(20))

  def testNoRatesTurnPacingOff(self):
    self.launchPaced(0, 0)
    events = [line.event for line in self.log("frame-control")]
    self.assertEqual(events.count("framePacingOff"), 1)
    self.assertNotIn("framePacingEnabled", events)
