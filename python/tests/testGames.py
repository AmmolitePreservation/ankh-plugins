import tempfile
import unittest
from pathlib import Path

from ankhPlugins.games import Game, repositoryFolder

fixtureRoot = repositoryFolder / "fixtureRoot"
somaFolder = Path("C:/Program Files (x86)/Steam/steamapps/common/SOMA")
jadeEmpireFolder = Path("C:/Program Files (x86)/Steam/steamapps/common/Jade Empire")
sampleSettings = ["executable=sample.exe", "architecture=x64", "loader=version"]


class TestGames(unittest.TestCase):
  def sampleGameError(self, extraSettings):
    """Returns the game.cfg path and the error of a game in a root of its own whose game.cfg adds these lines."""
    root = Path(self.enterContext(tempfile.TemporaryDirectory()))
    configPath = root / "games" / "sample" / "game.cfg"
    configPath.parent.mkdir(parents=True)
    configPath.write_text("\n".join(sampleSettings + extraSettings) + "\n", encoding="ascii")
    with self.assertRaises(ValueError) as raised:
      Game(root, "sample", "C:/sample")
    return configPath, str(raised.exception)

  def testSomaRunsSomaExeFromItsInstallFolder(self):
    game = Game(repositoryFolder, "soma", somaFolder)
    self.assertEqual(game.executablePath, somaFolder / "Soma.exe")
    self.assertEqual(game.executableName, "Soma.exe")
    self.assertEqual(game.pluginsFolder, somaFolder / "ankh-plugins")
    self.assertEqual(game.architecture, "x64")

  def testJadeEmpiresCompanionIsBesideItsExecutable(self):
    game = Game(repositoryFolder, "jade-empire", jadeEmpireFolder)
    self.assertEqual(game.companionPaths, [jadeEmpireFolder / "JadeEmpireConfig.exe"])

  def testSomaHasNoCompanions(self):
    self.assertEqual(Game(repositoryFolder, "soma", somaFolder).companionPaths, [])

  def testFixtureX64NamesTheBuildOfItsExecutable(self):
    self.assertEqual(Game(fixtureRoot, "fixture-x64", "C:/fixture").timeDateStamp, 0x9B5E3C7A)

  def testAGameThatNamesNoBuildHasNoTimeDateStamp(self):
    self.assertIsNone(Game(fixtureRoot, "fixture-x86-opengl32", "C:/fixture").timeDateStamp)

  def testAnUnknownGameNamesItsRootsGames(self):
    with self.assertRaises(ValueError) as raised:
      Game(fixtureRoot, "nosuchgame", "C:/")
    self.assertEqual(str(raised.exception), f"no game named 'nosuchgame' in {fixtureRoot / 'games'}; "
      "the games are fixture-x64, fixture-x86, fixture-x86-opengl32")

  def testATimeDateStampInLowercaseIsRefused(self):
    configPath, error = self.sampleGameError(["timeDateStamp=0x9b5e3c7a"])
    self.assertEqual(error,
      f"{configPath}: timeDateStamp must be 0x and eight hexadecimal digits in capitals, not '0x9b5e3c7a'")

  def testASecondTimeDateStampIsRefused(self):
    configPath, error = self.sampleGameError(["timeDateStamp=0x9B5E3C7A", "timeDateStamp=0x4D2A8F16"])
    self.assertEqual(error, f"{configPath} sets timeDateStamp more than once")


if __name__ == "__main__":
  unittest.main()
