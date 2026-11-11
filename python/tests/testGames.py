import unittest
from pathlib import Path

from ankhPlugins.games import Game


class TestGames(unittest.TestCase):
  def testSomaRunsSomaExeFromItsInstallFolder(self):
    game = Game("soma", "C:/Program Files (x86)/Steam/steamapps/common/SOMA")
    self.assertEqual(game.executablePath, Path("C:/Program Files (x86)/Steam/steamapps/common/SOMA/Soma.exe"))
    self.assertEqual(game.executableName, "Soma.exe")
    self.assertEqual(game.pluginsFolder, Path("C:/Program Files (x86)/Steam/steamapps/common/SOMA/ankh-plugins"))
    self.assertEqual(game.architecture, "x64")

  def testJadeEmpiresCompanionIsBesideItsExecutable(self):
    game = Game("jade-empire", "C:/Program Files (x86)/Steam/steamapps/common/Jade Empire")
    self.assertEqual(game.companionPaths, [Path("C:/Program Files (x86)/Steam/steamapps/common/Jade Empire/JadeEmpireConfig.exe")])

  def testSomaHasNoCompanions(self):
    self.assertEqual(Game("soma", "C:/Program Files (x86)/Steam/steamapps/common/SOMA").companionPaths, [])

  def testFixtureX64NamesTheBuildOfItsExecutable(self):
    self.assertEqual(Game("fixture-x64", "C:/fixture").timeDateStamp, 0x9B5E3C7A)

  def testAGameThatNamesNoBuildHasNoTimeDateStamp(self):
    self.assertIsNone(Game("fixture-x86-opengl32", "C:/fixture").timeDateStamp)

  def testAnUnknownGameNamesTheKnownOnes(self):
    with self.assertRaisesRegex(ValueError, r"no game named 'nosuchgame'; the games are .*\bsoma\b"):
      Game("nosuchgame", "C:/")


if __name__ == "__main__":
  unittest.main()
