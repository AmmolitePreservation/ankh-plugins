"""Runs display-guard in the fixture game acting as a Direct3D 9 game. The game asks for a framed 640x480 window and a
500x400 back buffer, or for fullscreen; the tests compare what it got with the primary monitor."""
from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import waitFor
from direct3DFixture import Direct3DFixtureTest, size

windowed = ["windowed", "500", "400"]
requestedSize = (500, 400)
messageSeconds = 10
sizeRestored = "0"
sizeMinimized = "1"
showWithoutActivating = 4

styleVisible = 0x10000000
stylePopup = 0x80000000
# WS_CAPTION, WS_THICKFRAME, WS_SYSMENU, WS_MINIMIZEBOX, WS_MAXIMIZEBOX and WS_BORDER.
styleFrame = 0x00C00000 | 0x00040000 | 0x00080000 | 0x00020000 | 0x00010000 | 0x00800000
# DISCL_EXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY, and DISCL_NONEXCLUSIVE | DISCL_FOREGROUND.
requestedCooperativeLevel = "0x15"
releasedCooperativeLevel = "0x06"

importedByTheFixture = ["ChangeDisplaySettingsA", "SetWindowLongA", "SetWindowPos", "GetClientRect", "ClientToScreen",
  "ScreenToClient", "SetForegroundWindow", "SetCursorPos", "ClipCursor"]


def letterbox(source, target):
  """(left, top, width, height) of the largest rectangle of the source's shape centered in a target at (0, 0)."""
  sourceWidth, sourceHeight = source
  targetWidth, targetHeight = target
  width = targetWidth
  height = targetWidth * sourceHeight // sourceWidth
  if height > targetHeight:
    height = targetHeight
    width = targetHeight * sourceWidth // sourceHeight
  return ((targetWidth - width) // 2, (targetHeight - height) // 2, width, height)


def nearestPixel(position, pixelCount):
  return min(max(position, 0), pixelCount - 1)


class DisplayGuardTest(Direct3DFixtureTest):
  def launchGuarded(self, arguments, **settings):
    self.monitor = windowsApi.primaryMonitorSize()
    self.launch(arguments, {"display-guard": settings})

  def picture(self, backBuffer):
    """Where display-guard presents the back buffer: its shape kept, unless the game replaces the placement with
    its own handler, as one fixture game does with a square."""
    gameHandler = self.game.folder / "handlers" / "display-guard" / "presentationPolicy.cpp"
    return letterbox((1, 1) if gameHandler.is_file() else backBuffer, self.monitor)

  def clientPosition(self, backBuffer, x, y):
    """The window position at the center of a back buffer pixel."""
    left, top, width, height = self.picture(backBuffer)
    return (left + (2 * x + 1) * width // (2 * backBuffer[0]), top + (2 * y + 1) * height // (2 * backBuffer[1]))

  def backBufferPosition(self, backBuffer, x, y):
    """The back buffer pixel the game is told for a window position."""
    left, top, width, height = self.picture(backBuffer)
    return (nearestPixel((x - left) * backBuffer[0] // width, backBuffer[0]),
      nearestPixel((y - top) * backBuffer[1] // height, backBuffer[1]))

  def borderlessStyle(self):
    (created,) = self.reported("windowCreated")
    return (int(created["style"], 16) & ~styleFrame) | stylePopup

  def assertTheWindowCoversTheMonitor(self):
    window = self.gameWindow()
    self.assertEqual(windowsApi.windowRectangle(window), (0, 0, *self.monitor))
    self.assertEqual(windowsApi.windowStyle(window), self.borderlessStyle() | styleVisible)

  def assertTheGameSeesItsBackBuffer(self, backBuffer):
    self.assertEqual([size(fields) for fields in self.reported("backBuffer")], [backBuffer, backBuffer])
    self.assertEqual([size(fields) for fields in self.reported("clientRectangle")], [backBuffer])
    self.assertEqual({size(fields) for fields in self.reported("windowSize") if fields["kind"] == sizeRestored}, {backBuffer})
    corners = [(0, 0), (backBuffer[0] - 1, backBuffer[1] - 1)]
    conversions = self.reported("pointConversion")
    self.assertEqual([size(fields, "x", "y") for fields in conversions], corners)
    self.assertEqual([size(fields, "screenX", "screenY") for fields in conversions],
      [self.clientPosition(backBuffer, x, y) for x, y in corners])
    # A picture narrower than the back buffer has fewer positions than the back buffer has pixels, so the way back
    # from one can end on the pixel beside the one it came from.
    self.assertEqual([size(fields, "backX", "backY") for fields in conversions],
      [self.backBufferPosition(backBuffer, *self.clientPosition(backBuffer, x, y)) for x, y in corners])

  def testNativeResolutionGivesTheGameABackBufferOfTheMonitorsSize(self):
    self.launchGuarded(windowed)
    for event in ("deviceCreated", "deviceReset"):
      with self.subTest(event=event):
        (applied,) = self.reported(event)
        self.assertEqual(size(applied, "backBufferWidth", "backBufferHeight"), self.monitor)
        self.assertEqual(applied["windowed"], "1")
        (logged,) = self.logged("display-guard", event)
        self.assertEqual(size(logged), requestedSize)
        self.assertEqual(size(logged, "appliedWidth", "appliedHeight"), self.monitor)
    self.assertTheGameSeesItsBackBuffer(self.monitor)
    self.assertTheWindowCoversTheMonitor()

  def testTheGamesOwnResolutionIsPresentedIntoThePicture(self):
    self.launchGuarded(windowed, renderResolution="game")
    for event in ("deviceCreated", "deviceReset"):
      with self.subTest(event=event):
        (applied,) = self.reported(event)
        self.assertEqual(size(applied, "backBufferWidth", "backBufferHeight"), requestedSize)
    self.assertTheGameSeesItsBackBuffer(requestedSize)
    self.assertTheWindowCoversTheMonitor()
    left, top, width, height = self.picture(requestedSize)
    self.assertEqual(self.logged("display-guard", "presentationChanged"), [{"sourceWidth": "500", "sourceHeight": "400",
      "left": str(left), "top": str(top), "width": str(width), "height": str(height)}])

  def testMousePositionsReachTheGameInBackBufferPixels(self):
    self.launchGuarded(windowed, renderResolution="game")
    left, top, width, height = self.picture(requestedSize)
    # Inside the picture, and outside it where a letterboxed picture has a bar: left of it or above it.
    positions = [(left + width // 3, top + height // 4), (left // 2, top + height // 2), (left + width // 2, top // 2)]
    for x, y in positions:
      with self.subTest(x=x, y=y):
        expected = self.backBufferPosition(requestedSize, x, y)
        windowsApi.postMouseMove(self.gameWindow(), x, y)
        waitFor(lambda: expected in [size(fields, "x", "y") for fields in self.reported("mouseMove")], messageSeconds,
          f"the game's mouse move at {expected}")

  def testTheGamesRestylingAndMovingLeaveTheWindowBorderlessOnTheMonitor(self):
    self.launchGuarded(windowed)
    (placement,) = self.reported("windowPlacement")
    self.assertEqual(placement, {"moveResult": "1", "hidden": "1", "showResult": "1", "shown": "1",
      "style": f"0x{self.borderlessStyle() | styleVisible:08X}", "left": "0", "top": "0", "width": str(self.monitor[0]),
      "height": str(self.monitor[1])})
    (restyled,) = self.logged("display-guard", "gameWindowRestyled")
    self.assertEqual(restyled["applied"], f"0x{self.borderlessStyle() | styleVisible:08X}")
    self.assertEqual(len(self.logged("display-guard", "gameWindowMoveReplaced")), 2)
    self.assertTheWindowCoversTheMonitor()

  def testTheGameKeepsItsSizeWhenItsWindowIsMinimizedAndRestored(self):
    self.launchGuarded(windowed, renderResolution="game")
    window = self.gameWindow()
    windowsApi.showWindow(window, windowsApi.showMinimizedWithoutActivating)
    waitFor(lambda: windowsApi.isMinimized(window), messageSeconds, "the minimized game window")
    sizesBefore = len(self.reported("windowSize"))
    windowsApi.showWindow(window, showWithoutActivating)
    waitFor(lambda: len(self.reported("windowSize")) > sizesBefore, messageSeconds, "the restored window's size message")
    waitFor(lambda: windowsApi.windowRectangle(window) == (0, 0, *self.monitor), messageSeconds,
      "the restored window covering the monitor")
    sizes = self.reported("windowSize")
    self.assertIn(sizeMinimized, [fields["kind"] for fields in sizes[:sizesBefore]])
    self.assertEqual([(fields["kind"], size(fields)) for fields in sizes[sizesBefore:]], [(sizeRestored, requestedSize)])
    self.assertTheWindowCoversTheMonitor()

  def testAFullscreenGameIsWindowedAndTheDisplayModeStays(self):
    self.launchGuarded(["fullscreen"])
    (requested,) = self.logged("display-guard", "deviceCreated")
    self.assertEqual(requested["requestedWindowed"], "0")
    (applied,) = self.reported("deviceCreated")
    self.assertEqual(applied["windowed"], "1")
    self.assertEqual(size(applied, "backBufferWidth", "backBufferHeight"), self.monitor)
    self.assertEqual(len(self.logged("display-guard", "displayModeChangeIgnored")), 1)
    (modeChange,) = self.reported("displayModeChange")
    self.assertEqual(modeChange["result"], "0")
    self.assertTheWindowCoversTheMonitor()

  def testStartMinimizedShowsTheGameMinimized(self):
    self.launchGuarded(windowed, startMinimized="true")
    self.assertTrue(windowsApi.isMinimized(self.gameWindow()))
    self.assertTrue(windowsApi.isVisible(self.gameWindow()))
    self.assertEqual(len(self.logged("display-guard", "startupMinimized")), 1)

  def testABackgroundGameCannotTakeTheForegroundCursorOrKeyboard(self):
    self.launchGuarded(windowed)
    self.assertFalse(self.process.isForeground())
    self.assertEqual(self.reported("foregroundRequest"), [{"result": "0"}])
    self.assertEqual(self.reported("cursorMove"), [{"result": "1", "moved": "0"}])
    self.assertEqual(self.reported("cursorClip"), [{"result": "1", "changed": "0"}])
    self.assertEqual(self.reported("cooperativeLevel"), [{"result": "0x00000000"}])
    events = [line.event for line in self.log("display-guard")]
    for event in ("inputReleased", "foregroundRequestIgnored", "cursorMoveIgnored", "cursorClipIgnored"):
      with self.subTest(event=event):
        self.assertEqual(events.count(event), 1)
    self.assertEqual(self.logged("display-guard", "cooperativeLevelSet"), [{"device": "keyboard",
      "requested": requestedCooperativeLevel, "applied": releasedCooperativeLevel, "result": "0x00000000"}])

  def testReleaseInputOffLeavesTheCursorAndKeyboardToTheGame(self):
    self.launchGuarded(windowed, releaseInput="false")
    self.assertEqual(self.reported("cursorMove"), [{"result": "1", "moved": "1"}])
    self.assertEqual(self.reported("cursorClip"), [{"result": "1", "changed": "1"}])
    events = [line.event for line in self.log("display-guard")]
    self.assertEqual(events.count("inputLeftToTheGame"), 1)
    for event in ("inputReleased", "cursorMoveIgnored", "cursorClipIgnored", "cooperativeLevelSet"):
      with self.subTest(event=event):
        self.assertNotIn(event, events)

  def testOnlyTheFunctionsTheGameImportsAreHooked(self):
    self.launchGuarded(windowed)
    messages = [line.message for line in self.log("display-guard")]
    hooked = [message for message in messages if message.startswith("importHooked ")]
    self.assertEqual(sorted(hooked), sorted(f"importHooked target=import of user32!{name}" for name in importedByTheFixture))
    self.assertIn("importAbsent target=import of user32!GetAsyncKeyState", messages)
