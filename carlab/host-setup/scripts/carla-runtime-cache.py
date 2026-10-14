#!/usr/bin/env python3
"""Mirror engine ELF modules on NVMe; preserve the source asset tree via links.

Only opt-in launchers use this cache. Refresh under one interprocess lock before
launch, atomically replace changed files, and prefetch the immutable copies.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import time


def signature(path):
    st = path.stat()
    return [st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def link(source, target):
    if target.is_symlink():
        if target.readlink() == source:
            return
        target.unlink()
    if target.exists():
        raise RuntimeError(f'Refusing to replace an unmanaged cache entry: {target}')
    target.symlink_to(source, target_is_directory=source.is_dir())


def prepare(source, cache, verify=False):
    source = source.resolve(strict=True)
    cache = cache.absolute()
    if cache == source or source in cache.parents or cache in source.parents:
        raise ValueError('Source and cache must be separate trees')
    src = source / 'Engine/Binaries/Linux'
    if not (src / 'UnrealEditor').is_file():
        raise ValueError('Source is not a compiled Unreal engine')
    cache.mkdir(parents=True, exist_ok=True)
    with (cache / '.refresh.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        manifest_path = cache / 'manifest.json'
        previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
        if previous and previous.get('source') != str(source):
            raise ValueError('Cache belongs to a different source engine')
        for entry in source.iterdir():
            if entry.name != 'Engine':
                link(entry, cache / entry.name)
        engine = cache / 'Engine'
        engine.mkdir(exist_ok=True)
        for entry in (source / 'Engine').iterdir():
            if entry.name != 'Binaries':
                link(entry, engine / entry.name)
        binaries = engine / 'Binaries'
        binaries.mkdir(exist_ok=True)
        for entry in (source / 'Engine/Binaries').iterdir():
            if entry.name != 'Linux':
                link(entry, binaries / entry.name)
        dst = binaries / 'Linux'
        dst.mkdir(exist_ok=True)
        managed = {}
        changed = 0
        for entry in src.iterdir():
            target = dst / entry.name
            if not entry.is_file() or not (entry.name.endswith('.so') or entry.name == 'UnrealEditor' or entry.suffix == '.modules'):
                link(entry, target)
                continue
            before = signature(entry)
            old = previous.get('files', {}).get(entry.name, {})
            if target.exists() and not target.is_symlink() and not old:
                raise RuntimeError(f'Refusing to overwrite an unmanaged binary: {target}')
            valid = target.is_file() and not target.is_symlink() and old.get('source_signature') == before and old.get('cache_signature') == signature(target)
            if not valid:
                temp = target.with_name(target.name + '.copying')
                if target.is_symlink():
                    target.unlink()
                shutil.copy2(entry, temp)
                sha = digest(temp)
                if before != signature(entry) or sha != digest(entry):
                    temp.unlink()
                    raise RuntimeError(f'Engine changed during cache refresh: {entry}')
                os.replace(temp, target)
                changed += 1
            else:
                sha = old['sha256']
            if verify and (digest(entry) != sha or digest(target) != sha):
                raise RuntimeError(f'Cache integrity check failed: {entry}')
            managed[entry.name] = {'source_signature': before, 'cache_signature': signature(target), 'sha256': sha}
        for name in previous.get('files', {}):
            if name not in managed:
                stale = dst / name
                if stale.is_file() and not stale.is_symlink():
                    stale.unlink()
        manifest = {'source': str(source), 'cache': str(cache), 'files': managed, 'updated': time.time()}
        temp_manifest = cache / 'manifest.json.copying'
        temp_manifest.write_text(json.dumps(manifest, indent=2) + '\n')
        os.replace(temp_manifest, manifest_path)
        # Advisory read-ahead is bounded to cached files, without a duplicate
        # userspace allocation. Parallel requests let NVMe service its queue.
        def prefetch(name):
            with (dst / name).open('rb') as stream:
                os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_WILLNEED)
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(prefetch, managed))
        return {'cache': str(cache), 'files': len(managed), 'changed': changed,
                'bytes': sum(item['cache_signature'][2] for item in managed.values()), 'verified': verify}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    print('CARLA_RUNTIME_CACHE ' + json.dumps(prepare(args.source, args.cache, args.verify)), flush=True)
