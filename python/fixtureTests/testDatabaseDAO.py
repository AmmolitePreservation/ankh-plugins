"""database-dao in the fixture game run as a game built with DAO 3.5 (its daoMode.cpp), in a copy of the install that
build.ps1 -test makes: it asks COM for DAO 3.5's DBEngine, which the plugin points at DAO 3.6's, makes an engine and
reports its Version."""
import os
import shutil
import unittest
import winreg

from ankhPlugins import windowsApi
from ankhPlugins.gameProcess import GameProcess, waitFor
from ankhPlugins.games import Game
from ankhPlugins.pluginLogs import sessionErrors, sessionLog

reportFileName = "fixtureReport.txt"
copyFolderName = "fixtureInstallDAO"
pluginFileName = "database-dao.dll"
readySeconds = 30
exitSeconds = 10
dao35DBEngine = "{00000010-0000-0010-8000-00AA006D2EA4}"
dao36DBEngine = "{00000100-0000-0010-8000-00AA006D2EA4}"
classNotRegistered = "0x80040154"


def isRegistered32BitClass(classID):
  try:
    winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, f"CLSID\\{classID}\\InprocServer32", 0,
      winreg.KEY_READ | winreg.KEY_WOW64_32KEY).Close()
    return True
  except FileNotFoundError:
    return False


class DatabaseDAOTest(unittest.TestCase):
  def setUp(self):
    installed = Game(os.environ["ankhPluginsFixtureRoot"], os.environ["ankhPluginsFixtureGame"],
      os.environ["ankhPluginsFixtureFolder"])
    pluginLines = (installed.definitionFolder / "plugins.txt").read_text(encoding="ascii").splitlines()
    if pluginFileName not in pluginLines:
      self.skipTest(f"{installed.name} does not load {pluginFileName}")
    if installed.architecture != "x86":
      self.skipTest("DAO is 32-bit only")
    if not isRegistered32BitClass(dao36DBEngine):
      self.skipTest("this Windows has no 32-bit DAO 3.6 to point DAO 3.5 at")
    if isRegistered32BitClass(dao35DBEngine):
      self.skipTest("this Windows has DAO 3.5 itself, so nothing shows what the plugin changes")
    self.installed = installed

  def launch(self, isPluginOn):
    """Starts a copy of the install in its DAO mode, with database-dao on or switched off, and returns once its report
    ends."""
    copyFolder = self.installed.installFolder.with_name(copyFolderName)
    if copyFolder.exists():
      shutil.rmtree(copyFolder)
    shutil.copytree(self.installed.installFolder, copyFolder, ignore=shutil.ignore_patterns("logs", reportFileName))
    self.game = Game(self.installed.root, self.installed.name, copyFolder)
    if not isPluginOn:
      pluginsPath = self.game.definitionFolder / "plugins.txt"
      lines = pluginsPath.read_text(encoding="ascii").splitlines()
      pluginsPath.write_text("".join(f"#{line}\n" if line == pluginFileName else f"{line}\n" for line in lines),
        encoding="ascii")
    self.process = GameProcess.launch(self.game, ["dao"])
    self.addCleanup(self.process.terminate, exitSeconds)
    waitFor(lambda: {"ready", "failed"} & {event for event, _ in self.report()} or self.process.exitCode is not None,
      readySeconds, "the end of the fixture game's report")

  def report(self):
    path = self.game.executablePath.parent / reportFileName
    if not path.is_file():
      return []
    lines = []
    for rawLine in path.read_text(encoding="ascii").splitlines():
      event, *tokens = rawLine.split(" ")
      lines.append((event, dict(token.split("=", 1) for token in tokens)))
    return lines

  def testDAO35sEngineIsDAO36sWithThePlugin(self):
    self.launch(isPluginOn=True)
    self.assertEqual(self.report(), [
      ("daoClassObject", {"result": "0x00000000"}),
      ("daoEngine", {"version": "3.6"}),
      ("ready", {}),
    ])
    pluginLog = sessionLog(self.game.pluginsFolder, "database-dao", self.process.processID, self.process.startedAt)
    self.assertEqual([(line.event, line.fields) for line in pluginLog if line.event.startswith("dao")], [
      ("daoRedirectInstalled", {"CoGetClassObject": "hooked", "CoCreateInstance": "notImported",
                                "CoCreateInstanceEx": "notImported"}),
      ("daoClassRedirected", {"call": "CoGetClassObject", "from": "00000010", "to": "00000100", "result": "0x00000000"}),
    ])
    self.assertEqual(sessionErrors(self.game.pluginsFolder, self.process.processID, self.process.startedAt), [])

  def testWithoutThePluginDAO35IsNotThere(self):
    self.launch(isPluginOn=False)
    self.assertEqual(self.report(), [
      ("daoClassObject", {"result": classNotRegistered}),
      ("failed", {"step": "classObject", "error": classNotRegistered}),
    ])
    self.assertTrue(windowsApi.waitForExit(self.process.handle, exitSeconds))
    self.assertEqual(self.process.exitCode, 9)


if __name__ == "__main__":
  unittest.main()
