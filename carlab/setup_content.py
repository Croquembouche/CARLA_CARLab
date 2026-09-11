"""Restore pinned stock content, then apply CARLab overlays. Run before building."""
from pathlib import Path
import subprocess,json,shutil,hashlib,os
root=Path(__file__).resolve().parents[1];meta=json.loads((root/'carlab/upstream-lock.json').read_text());target=root/'Unreal/CarlaUnreal/Content';stock=target/'Carla'
def git(*args):return subprocess.check_output(['git','-C',str(stock),*args],text=True).strip()
if not stock.exists():
 target.mkdir(parents=True,exist_ok=True)
 subprocess.run(['git','clone','--no-checkout',meta['content']['repository'],str(stock)],check=True)
 subprocess.run(['git','-C',str(stock),'checkout',meta['content']['commit']],check=True,env=dict(os.environ,GIT_LFS_SKIP_SMUDGE='1'))
else:
 if git('rev-parse','HEAD')!=meta['content']['commit']:raise SystemExit('Existing content has a different revision; preserve it and prepare a clean content checkout.')
 if git('status','--porcelain'):raise SystemExit('Existing content has local changes; preserve them before applying the CARLab overlay.')
subprocess.run(['git','-C',str(stock),'lfs','pull'],check=True)
for source in (root/'carlab/content-overrides').iterdir():
 shutil.copytree(source,target/source.name,dirs_exist_ok=True)
for source in (root/'carlab/config-overrides').glob('*'):
 shutil.copy2(source,root/'Unreal/CarlaUnreal/Config'/source.name)
print('Pinned content and CARLab overrides installed.')
