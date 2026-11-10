"""The plugins' session logs: logs\\<plugin>\\<plugin>_<date>_<time>_pid<processID>.log in the ankh-plugins folder
beside the game's executable."""
import re
from datetime import datetime
from pathlib import Path

logsFolderName = "logs"
linePattern = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (INFO|WARN|ERROR) (.*)$")
fileNamePattern = re.compile(r"^(?P<plugin>.+)_(?P<stamp>\d{8}_\d{6})_pid(?P<processID>\d+)\.log$")
levelRanks = {"INFO": 0, "WARN": 1, "ERROR": 2}


class LogLine:
  def __init__(self, rawLine):
    match = linePattern.match(rawLine)
    if match is None:
      raise ValueError(f"malformed log line: {rawLine!r}")
    self.timestamp, self.level, self.message = match.groups()
    tokens = self.message.split(" ")
    self.event = tokens[0]
    self.fields = dict(token.split("=", 1) for token in tokens[1:] if "=" in token)

  def __str__(self):
    return f"{self.timestamp} {self.level} {self.message}"


def logsDirectory(pluginsFolder):
  return Path(pluginsFolder) / logsFolderName


def sessionLogPaths(pluginsFolder, processID, startedAt):
  """Every plugin's log for the game with this process ID that started at startedAt (seconds since the epoch).
  Windows reuses process IDs and logs are kept 90 days, so the session stamp, the local time the loader
  started, must not be earlier than the process's creation second."""
  earliest = datetime.fromtimestamp(int(startedAt))
  found = {}
  for path in logsDirectory(pluginsFolder).glob(f"*/*_pid{processID}.log"):
    match = fileNamePattern.match(path.name)
    if match is None or int(match["processID"]) != processID or match["plugin"] != path.parent.name:
      continue
    if datetime.strptime(match["stamp"], "%Y%m%d_%H%M%S") < earliest:
      continue
    if match["plugin"] in found:
      raise FileNotFoundError(f"two {match['plugin']} logs for process {processID} since {earliest}: {found[match['plugin']].name}, {path.name}")
    found[match["plugin"]] = path
  return found


def sessionLogPath(pluginsFolder, pluginName, processID, startedAt):
  paths = sessionLogPaths(pluginsFolder, processID, startedAt)
  if pluginName not in paths:
    raise FileNotFoundError(f"no {pluginName} log for process {processID} in {logsDirectory(pluginsFolder) / pluginName}")
  return paths[pluginName]


def readLogLines(path):
  with open(path, encoding="ascii", errors="replace") as logFile:
    return [LogLine(rawLine.rstrip("\r\n")) for rawLine in logFile if rawLine.strip()]


def sessionLog(pluginsFolder, pluginName, processID, startedAt, minimumLevel="INFO"):
  if minimumLevel not in levelRanks:
    raise ValueError(f"minimumLevel is one of {', '.join(levelRanks)}, not {minimumLevel!r}")
  lines = readLogLines(sessionLogPath(pluginsFolder, pluginName, processID, startedAt))
  return [line for line in lines if levelRanks[line.level] >= levelRanks[minimumLevel]]


def sessionErrors(pluginsFolder, processID, startedAt):
  """(plugin, line) for every ERROR line any plugin has logged in this game."""
  errors = []
  for pluginName, path in sorted(sessionLogPaths(pluginsFolder, processID, startedAt).items()):
    errors.extend((pluginName, line) for line in readLogLines(path) if line.level == "ERROR")
  return errors
