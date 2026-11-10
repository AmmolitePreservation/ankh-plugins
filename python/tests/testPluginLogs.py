import shutil
import tempfile
import unittest
from datetime import datetime
from pathlib import Path

from ankhPlugins.pluginLogs import LogLine, sessionErrors, sessionLog, sessionLogPath


def epochSeconds(text):
  return datetime.strptime(text, "%Y-%m-%d %H:%M:%S").timestamp()


class TestSessionLogs(unittest.TestCase):
  def setUp(self):
    self.pluginsFolder = Path(tempfile.mkdtemp(prefix="ankhPluginsLogs"))
    self.addCleanup(shutil.rmtree, self.pluginsFolder)

  def writeLog(self, pluginName, fileName, *lines):
    directory = self.pluginsFolder / "logs" / pluginName
    directory.mkdir(parents=True, exist_ok=True)
    (directory / fileName).write_text("".join(line + "\r\n" for line in lines), encoding="ascii")

  def testTheLogIsFoundByProcessID(self):
    self.writeLog("loader", "loader_20261001_184505_pid23064.log", "2026-10-01 18:45:05.385 INFO sessionStart version=0.1.0")
    self.writeLog("loader", "loader_20261001_184135_pid5152.log", "2026-10-01 18:41:35.444 INFO sessionStart version=0.1.0")
    path = sessionLogPath(self.pluginsFolder, "loader", 5152, epochSeconds("2026-10-01 18:41:34"))
    self.assertEqual(path.name, "loader_20261001_184135_pid5152.log")

  def testAReusedProcessIDPicksTheLogOfTheGameThatStartedThen(self):
    self.writeLog("loader", "loader_20260801_100000_pid1660.log", "2026-08-01 10:00:00.000 INFO loadComplete")
    self.writeLog("loader", "loader_20261003_210341_pid1660.log", "2026-10-03 21:03:41.139 INFO sessionStart version=0.1.0")
    path = sessionLogPath(self.pluginsFolder, "loader", 1660, epochSeconds("2026-10-03 21:03:40"))
    self.assertEqual(path.name, "loader_20261003_210341_pid1660.log")

  def testAnOlderGamesLogDoesNotCountForANewGameWithTheSameProcessID(self):
    self.writeLog("loader", "loader_20260801_100000_pid1660.log", "2026-08-01 10:00:00.000 INFO loadComplete")
    with self.assertRaises(FileNotFoundError):
      sessionLogPath(self.pluginsFolder, "loader", 1660, epochSeconds("2026-10-03 21:03:40"))

  def testAProcessWithoutALogIsAnError(self):
    with self.assertRaises(FileNotFoundError):
      sessionLogPath(self.pluginsFolder, "loader", 77, epochSeconds("2026-10-01 00:00:00"))

  def testProcessIDsAreMatchedWhole(self):
    self.writeLog("loader", "loader_20261001_184135_pid15152.log", "2026-10-01 18:41:35.444 INFO sessionStart version=0.1.0")
    with self.assertRaises(FileNotFoundError):
      sessionLogPath(self.pluginsFolder, "loader", 5152, epochSeconds("2026-10-01 18:41:34"))

  def testMinimumLevelFiltersLines(self):
    self.writeLog("loader", "loader_20261001_184135_pid5152.log",
      "2026-10-01 18:41:35.444 INFO sessionStart version=0.1.0",
      "2026-10-01 18:41:36.000 WARN noPluginsListed file=plugins.txt",
      "2026-10-01 18:41:37.000 ERROR could not load")
    lines = sessionLog(self.pluginsFolder, "loader", 5152, epochSeconds("2026-10-01 18:41:34"), "WARN")
    self.assertEqual([line.event for line in lines], ["noPluginsListed", "could"])

  def testAnUnknownLevelIsAnError(self):
    self.writeLog("loader", "loader_20261001_184135_pid5152.log", "2026-10-01 18:41:35.444 INFO sessionStart version=0.1.0")
    with self.assertRaises(ValueError):
      sessionLog(self.pluginsFolder, "loader", 5152, epochSeconds("2026-10-01 18:41:34"), "DEBUG")

  def testErrorsAreCollectedFromEveryPluginOfTheGame(self):
    self.writeLog("loader", "loader_20261001_184135_pid5152.log", "2026-10-01 18:41:35.444 INFO sessionStart version=0.1.0")
    self.writeLog("borderless-fit", "borderless-fit_20261001_184135_pid5152.log",
      "2026-10-01 18:41:35.500 INFO sessionStart version=0.1.0",
      "2026-10-01 18:41:35.501 ERROR The settings file has no value for 'x'")
    self.writeLog("borderless-fit", "borderless-fit_20260901_120000_pid5152.log", "2026-09-01 12:00:00.000 ERROR an older game's failure")
    errors = sessionErrors(self.pluginsFolder, 5152, epochSeconds("2026-10-01 18:41:34"))
    self.assertEqual([(pluginName, line.message) for pluginName, line in errors],
      [("borderless-fit", "The settings file has no value for 'x'")])


class TestLogLines(unittest.TestCase):
  def testEventAndFieldsAreSplitOut(self):
    line = LogLine("2026-10-01 18:41:35.444 INFO settingsLoaded file=borderless-fit.cfg count=3")
    self.assertEqual((line.level, line.event, line.fields), ("INFO", "settingsLoaded", {"file": "borderless-fit.cfg", "count": "3"}))

  def testAMalformedLineIsAnError(self):
    with self.assertRaises(ValueError):
      LogLine("not a log line")


if __name__ == "__main__":
  unittest.main()
