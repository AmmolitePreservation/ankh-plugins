"""Runs the loader and the echo plugin in the fixture game that build.ps1 -test installs. The build names the game's
root, the game and its install folder in the ankhPluginsFixtureRoot, ankhPluginsFixtureGame and
ankhPluginsFixtureFolder environment variables. Every test starts its own game."""
import os
import unittest

from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import GameProcess, waitFor
from ankhPlugins.games import Game
from ankhPlugins.linePipe import LoaderSession, PipeCommandError
from ankhPlugins.pluginLogs import sessionErrors, sessionLog
from ankhPlugins.session import waitForLoader

pumpSeconds = 10
exitSeconds = 10
# The loader's commandReplyCapacity.
commandReplyCapacity = 65536
echoCommands = ["echo", "pumpCount", "quit", "fail", "twoLines", "long", "handler"]


class LoaderTest(unittest.TestCase):
  def setUp(self):
    self.game = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    self.process = GameProcess.launch(self.game, [])
    self.addCleanup(self.process.terminate, exitSeconds)
    waitForLoader(self.process)
    try:
      self.session = LoaderSession(self.process.processID)
      self.addCleanup(self.session.close)
      # The game's main thread has a message queue from its first pump on, which the quit command needs.
      waitFor(lambda: self.pumpCount() > 0, pumpSeconds, "the game's first message pump")
    except (PipeCommandError, OSError):
      if self.process.exitCode is not None:
        self.fail(f"the fixture game exited with {self.process.exitCode} (its main.cpp lists the exit codes)")
      raise

  def pumpCount(self):
    return int(self.session.command("pumpCount")["count"])

  def log(self, pluginName):
    return sessionLog(self.game.pluginsFolder, pluginName, self.process.processID, self.process.startedAt)

  def errors(self):
    return sessionErrors(self.game.pluginsFolder, self.process.processID, self.process.startedAt)

  def testPluginsListsEveryLoadedPluginAndTheReloadableOnes(self):
    self.assertEqual(self.session.plugins(), ({"stability-guard.dll": 1, "display-guard.dll": 1, "echo.dll": 1}, ["echo.dll"]))

  def testEveryPluginLoadsOnceWithoutErrors(self):
    for pluginName in ("loader", "stability-guard", "display-guard", "echo"):
      with self.subTest(plugin=pluginName):
        self.assertEqual([line.event for line in self.log(pluginName)].count("loadComplete"), 1)
    self.assertEqual(self.errors(), [])

  def testCommandArgumentReachesThePluginAsSent(self):
    self.assertEqual(self.session.command("echo", "two words"), {"text": "two words"})
    self.assertEqual(self.session.sendLine("echo a b  c"), "ok text=a b  c")
    self.assertEqual(self.session.sendLine("echo"), "ok text=")

  def testPumpCallbacksRunOnTheGameMainThread(self):
    countBefore = self.pumpCount()
    waitFor(lambda: self.pumpCount() > countBefore, pumpSeconds, "another message pump")
    sessionStarts = [line for line in self.log("loader") if line.event == "sessionStart"]
    self.assertEqual(len(sessionStarts), 1)
    self.assertEqual(self.session.command("pumpCount")["threadID"], sessionStarts[0].fields["mainThreadID"])

  def testFailedCommandsAnswerWithAnErrorAndTheLoaderGoesOn(self):
    errorReplies = {
      "fail": "command 'fail' failed (-7)",
      "twoLines": "command 'twoLines' replied with more than one line",
      "nosuch": "unknown command 'nosuch'",
      "plugins all": "plugins takes no arguments",
      "reload": "reload takes one plugin DLL name",
      "reload echo.dll now": "reload takes one plugin DLL name",
      "reload nosuch.dll": "nosuch.dll is not a loaded plugin",
      "reload stability-guard.dll": "stability-guard.dll does not export Unload, so it loads only at game start",
    }
    for line, message in errorReplies.items():
      with self.subTest(line=line):
        self.assertEqual(self.session.sendLine(line), "error " + message)
    self.assertEqual(self.session.command("echo", "after"), {"text": "after"})

  def testAGamesHandlerFileReplacesThePluginsOwn(self):
    gameHandler = self.game.folder / "handlers" / "echo" / "handlerName.cpp"
    builtHandler = self.game.name if gameHandler.is_file() else "default"
    self.assertEqual(self.session.command("handler"), {"name": builtHandler})

  def testReplyFillingTheWholeBufferArrivesComplete(self):
    prefix = "ok text="
    self.assertEqual(self.session.sendLine("long"), prefix + "x" * (commandReplyCapacity - len(prefix)))

  def testReloadReplacesThePluginAndItsCommands(self):
    self.assertEqual(self.session.reload("echo.dll"), 2)
    self.assertEqual(self.session.plugins(), ({"stability-guard.dll": 1, "display-guard.dll": 1, "echo.dll": 2}, ["echo.dll"]))
    self.assertEqual(self.session.command("echo", "again"), {"text": "again"})
    waitFor(lambda: self.pumpCount() > 0, pumpSeconds, "a message pump in the reloaded plugin")
    loaderLines = self.log("loader")
    self.assertEqual([line.fields for line in loaderLines if line.event == "pluginUnloaded"], [{"name": "echo.dll", "generation": "1"}])
    registered = [line.fields["name"] for line in loaderLines if line.event == "commandRegistered" and line.fields["plugin"] == "echo.dll"]
    self.assertEqual(registered, echoCommands * 2)
    self.assertEqual([line.event for line in self.log("echo")],
      ["sessionStart", "loadComplete", "unloadComplete", "sessionStart", "loadComplete"])

  def testQuitEndsTheGameThroughItsMessageLoop(self):
    self.assertEqual(self.session.command("quit"), {})
    self.assertTrue(windowsApi.waitForExit(self.process.handle, exitSeconds))
    self.assertEqual(self.process.exitCode, 0)
    self.assertEqual(self.errors(), [])
