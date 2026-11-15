"""The installer refuses a RomFS folder holding one huge image file; synthetic sparse file, no game."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from installer import bananza as b
from installer.events import Reporter
from installer.runner import InstallError


class RomfsImage(unittest.TestCase):
    def test_image_refused(self):
        with tempfile.TemporaryDirectory() as d:
            with open(Path(d) / 'game.romfs', 'wb') as f:
                f.truncate((1 << 30) + 1)
            with self.assertRaises(InstallError) as e:
                b.inventory(Path(d), Reporter())
            self.assertEqual(e.exception.id if hasattr(e.exception, 'id') else e.exception.args[0], 'dkb.romfs_image')

    def test_tree_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'Shader').mkdir()
            (Path(d) / 'Shader' / 'a.bin').write_bytes(b'x')
            self.assertEqual(len(b.inventory(Path(d), Reporter())), 1)


if __name__ == '__main__':
    unittest.main()
