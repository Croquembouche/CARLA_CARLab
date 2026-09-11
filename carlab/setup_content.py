"""Restore pinned stock content, then apply CARLab overlays once, before building."""
from pathlib import Path
import json
import os
import shutil
import subprocess


def restore(root):
    root = Path(root).resolve()
    meta = json.loads((root / 'carlab/upstream-lock.json').read_text())
    target = root / 'Unreal/CarlaUnreal/Content'
    stock = target / 'Carla'
    overlays = root / 'carlab/content-overrides'
    configs = root / 'carlab/config-overrides'
    # Check before cloning or copying: a clone with skipped LFS downloads must
    # not install pointer text as an Unreal asset.
    for folder in (overlays, configs):
        for path in folder.rglob('*'):
            if path.is_file() and path.stat().st_size < 1024:
                if path.read_bytes().startswith(b'version https://git-lfs.github.com/spec/v1\n'):
                    raise SystemExit(f'Unexpanded LFS pointer: {path.relative_to(root)}. Run git lfs pull in this CARLA checkout first.')
    if not overlays.is_dir() or not configs.is_dir():
        raise SystemExit('Missing CARLab content/configuration overlays; use the complete published checkout.')

    def git(*args):
        return subprocess.check_output(['git', '-C', str(stock), *args], text=True).strip()

    if not stock.exists():
        target.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--no-checkout', meta['content']['repository'], str(stock)], check=True)
        subprocess.run(['git', '-C', str(stock), 'checkout', meta['content']['commit']],
                       check=True, env=dict(os.environ, GIT_LFS_SKIP_SMUDGE='1'))
    else:
        if not (stock / '.git').exists():
            raise SystemExit('Existing Content/Carla is not a stock Git checkout; preserve it and prepare a clean destination.')
        if git('rev-parse', 'HEAD') != meta['content']['commit']:
            raise SystemExit('Existing content has a different revision; preserve it and prepare a clean pinned checkout.')
        if git('status', '--porcelain'):
            raise SystemExit('Existing content has local changes (including any previously applied overlay). Preserve them; do not rerun first-install restoration during a rebuild.')
    subprocess.run(['git', '-C', str(stock), 'lfs', 'pull'], check=True)
    for source in overlays.iterdir():
        shutil.copytree(source, target / source.name, dirs_exist_ok=True)
    config_target = root / 'Unreal/CarlaUnreal/Config'
    config_target.mkdir(parents=True, exist_ok=True)
    for source in configs.iterdir():
        if source.is_file():
            shutil.copy2(source, config_target / source.name)
    print('Pinned content and CARLab overrides installed. Preserve this modified stock checkout during rebuilds.')


if __name__ == '__main__':
    restore(Path(__file__).resolve().parents[1])
