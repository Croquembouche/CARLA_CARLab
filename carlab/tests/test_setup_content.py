"""Exercise content restoration with a small local Git repository, not stock assets."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('setup_content', Path(__file__).resolve().parents[1] / 'setup_content.py')
setup_content = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup_content)


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True, stderr=subprocess.DEVNULL).strip()


class ContentRestoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='carlab-content-test-')
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.stock = base / 'upstream'
        self.stock.mkdir()
        git(self.stock, 'init', '-b', 'main')
        git(self.stock, 'config', 'user.email', 'fixture@example.invalid')
        git(self.stock, 'config', 'user.name', 'Fixture')
        (self.stock / 'map.umap').write_bytes(b'original')
        git(self.stock, 'add', '.')
        git(self.stock, 'commit', '-m', 'stock')
        self.root = base / 'destination'
        self.overlay = self.root / 'carlab/content-overrides/Carla/map.umap'
        self.overlay.parent.mkdir(parents=True)
        self.overlay.write_bytes(b'custom')
        configs = self.root / 'carlab/config-overrides'
        configs.mkdir()
        (configs / 'DefaultEngine.ini').write_text('custom configuration')
        self.meta = self.root / 'carlab/upstream-lock.json'
        self.meta.write_text(json.dumps({'content': {'repository': str(self.stock), 'commit': git(self.stock, 'rev-parse', 'HEAD')}}))
        self.target = self.root / 'Unreal/CarlaUnreal/Content/Carla'

    def test_fresh_restore(self):
        setup_content.restore(self.root)
        self.assertEqual((self.target / 'map.umap').read_bytes(), b'custom')
        self.assertEqual((self.root / 'Unreal/CarlaUnreal/Config/DefaultEngine.ini').read_text(), 'custom configuration')

    def test_modified_content_is_preserved(self):
        setup_content.restore(self.root)
        (self.target / 'map.umap').write_bytes(b'user edits')
        with self.assertRaisesRegex(SystemExit, 'local changes'):
            setup_content.restore(self.root)
        self.assertEqual((self.target / 'map.umap').read_bytes(), b'user edits')

    def test_pointer_rejected_before_destination_creation(self):
        self.overlay.write_text('version https://git-lfs.github.com/spec/v1\noid sha256:' + 'a' * 64 + '\nsize 9999\n')
        with self.assertRaisesRegex(SystemExit, 'LFS pointer'):
            setup_content.restore(self.root)
        self.assertFalse((self.root / 'Unreal').exists())

    def test_wrong_revision_is_preserved(self):
        self.target.parent.mkdir(parents=True)
        subprocess.run(['git', 'clone', '--quiet', str(self.stock), str(self.target)], check=True)
        meta = json.loads(self.meta.read_text())
        meta['content']['commit'] = 'b' * 40
        self.meta.write_text(json.dumps(meta))
        with self.assertRaisesRegex(SystemExit, 'different revision'):
            setup_content.restore(self.root)
        self.assertEqual((self.target / 'map.umap').read_bytes(), b'original')


if __name__ == '__main__':
    unittest.main()
