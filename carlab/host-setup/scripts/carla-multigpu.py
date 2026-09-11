#!/usr/bin/env python3
"""Supervise a CARLA primary and one independent Vulkan renderer per GPU."""
import argparse
import json
import os
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import time

import carla

ROOT = Path('/mnt/simulations/carla/carlab/host-setup')
LINUX = Path('/mnt/simulations')


def commands(gpus, port, backend):
    common = [str(LINUX / 'bin/carla-sim'), '-unattended', '-nosound', '-log']
    result = [('primary', common + [f'-carla-rpc-port={port}', '-nullrhi'])]
    settings = ['r.Streaming.PoolSize 2000', 'r.RayTracing.UseReferenceBasedResidency 1',
                'r.RayTracing.NumAlwaysResidentLODs 0,r.RayTracing.ResidentGeometryMemoryPoolSizeInMB 2048', 'carla.Sensors.GpuRayTracing ' + ('1' if backend == 'gpu' else '0')]
    if backend == 'gpu':
        settings += ['r.RayTracing.ExternalQueries 1', 'r.RayTracing.ExternalQueryBounds 1',
                     'r.RayTracing.ForceAllRayTracingEffects 0', 'r.RayTracing.Culling 0',
                     'r.RayTracing.Geometry.InstancedStaticMeshes.Culling 0']
    for worker, gpu in enumerate(gpus):
        result.append((f'gpu-{gpu}', common + [
            f'-carla-rpc-port={port + 10 * (worker + 1)}',
            '-carla-primary-host=127.0.0.1', f'-carla-primary-port={port + 2}',
            f'-graphicsadapter={gpu}', '-RenderOffScreen', '-ResX=160', '-ResY=90',
            '-quality-level=High', '-ExecCmds=' + ','.join(settings)]))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gpus', default='auto', help='auto starts without renderers; or comma-separated Vulkan adapters')
    p.add_argument('--port', type=int, default=2000)
    p.add_argument('--backend', choices=['gpu', 'cpu'], default='gpu')
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    pool = [3, 2, 1, 0]
    try:
        gpus = [] if args.gpus == 'auto' else [int(x) for x in args.gpus.split(',')]
        assert len(set(gpus)) == len(gpus) and all(x in pool for x in gpus)
        assert 1024 <= args.port <= 65000 - 40
    except (ValueError, AssertionError):
        p.error('Use auto or distinct GPU indices 0–3 and a valid unprivileged port.')
    # Stable per-adapter ports survive worker removal and re-creation.
    definitions = dict(commands(pool, args.port, args.backend))
    launch = [(r, definitions[r]) for r in ['primary'] + [f'gpu-{g}' for g in gpus]]
    if args.dry_run:
        print(json.dumps([{'role': r, 'command': c} for r, c in launch], indent=2)); return
    output = ROOT / 'logs' / time.strftime('multigpu-%Y%m%d-%H%M%S')
    output.mkdir(parents=True)
    children = {}; handles = []; initialized = set()
    manifest = {'backend': args.backend, 'primary_port': args.port, 'manager_pid': os.getpid(),
                'ready': False, 'pool': pool, 'requested_gpus': gpus, 'request_id': None, 'error': None}

    saved_manifest = None

    def save_manifest():
        nonlocal saved_manifest
        manifest['children'] = [{'role': r, 'pid': c.pid, 'command': c.args,
                                 'initialized': r in initialized} for r, c in children.items()]
        encoded=json.dumps(manifest,indent=2)
        if encoded==saved_manifest:return
        tmp = output / 'processes.json.next'; tmp.write_text(encoded); tmp.replace(output / 'processes.json')
        saved_manifest=encoded

    def stop(_signum, _frame): raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop); signal.signal(signal.SIGINT, stop)

    def start_child(role):
        cmd = definitions[role]
        port = int(next(x.split('=')[1] for x in cmd if x.startswith('-carla-rpc-port=')))
        for offset in range(3):
            with socket.socket() as probe:
                probe.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
                probe.bind(('0.0.0.0', port + offset))
        # Truncate logs when restarting this adapter; old readiness markers must not qualify.
        (output / (role + '.log')).write_text('')
        handle = (output / (role + '-stdout.log')).open('ab'); handles.append(handle)
        child = subprocess.Popen(cmd + ['-abslog=' + str(output / (role + '.log'))], cwd=LINUX,
                                 stdout=handle, stderr=subprocess.STDOUT, start_new_session=True)
        children[role] = child
        print(f'{role}: PID {child.pid}', flush=True)
        save_manifest()

    def stop_children(roles):
        # Ask every retiring renderer to exit together, then share one deadline.
        for role in roles:
            child=children[role]
            if child.poll() is None:
                try:os.killpg(child.pid,signal.SIGTERM)
                except ProcessLookupError:pass
        deadline=time.monotonic()+20
        for role in roles:
            child=children[role]
            try:child.wait(timeout=max(.1,deadline-time.monotonic()))
            except subprocess.TimeoutExpired:
                try:os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                child.wait()
            del children[role];initialized.discard(role)
        save_manifest()

    def inspect():
        for role, child in children.items():
            if child.poll() is not None: raise RuntimeError(f'{role} exited with code {child.returncode}; inspect {output}')
            if role not in initialized and 'Engine is initialized. Leaving FEngineLoop::Init()' in (output / (role + '.log')).read_text(errors='replace'):
                initialized.add(role)
        save_manifest()
        return len(initialized) == len(children)

    try:
        start_child('primary')
        deadline = time.monotonic() + 600
        while True:
            if children['primary'].poll() is not None: raise RuntimeError('Primary exited during startup')
            try:
                client = carla.Client('127.0.0.1', args.port); client.set_timeout(5)
                world = client.get_world(); world.get_map()
                settings = world.get_settings(); settings.synchronous_mode = True; settings.fixed_delta_seconds = .05
                world.apply_settings(settings); break
            except (OSError, RuntimeError):
                if time.monotonic() > deadline: raise TimeoutError('Primary startup exceeded 10 minutes')
                time.sleep(1)
        for gpu in gpus: start_child(f'gpu-{gpu}')
        deadline = time.monotonic() + 1800
        while not inspect():
            if time.monotonic() > deadline: raise TimeoutError('Renderer startup exceeded 30 minutes')
            time.sleep(1)
        client.set_timeout(180)
        for _ in range(10): world.tick(); time.sleep(.1)
        manifest['ready'] = True; save_manifest()
        print('READY: renderer pool accepts paused loadout changes.', flush=True)
        # After startup, only Control Center may advance the world clock.
        while True:
            inspect()
            request_path = output / 'workers-request.json'
            if request_path.exists():
                try:request = json.loads(request_path.read_text())
                except (FileNotFoundError,json.JSONDecodeError):
                    time.sleep(.05);continue
                if request.get('id') != manifest['request_id']:
                    desired = request['gpus']
                    if not isinstance(desired, list) or len(set(desired)) != len(desired) or any(g not in pool for g in desired):
                        raise ValueError('Invalid worker request')
                    manifest.update(request_id=request['id'], requested_gpus=desired, error=None, resizing=True)
                    save_manifest()
                    # Controller drains/destroys GPU subscriptions before requesting removal.
                    stop_children([role for role in children if role != 'primary' and int(role.split('-')[1]) not in desired])
                    for gpu in desired:
                        if f'gpu-{gpu}' not in children: start_child(f'gpu-{gpu}')
                    deadline = time.monotonic() + 1800
            if manifest.get('resizing'):
                if inspect(): manifest['resizing'] = False; save_manifest()
                elif time.monotonic() > deadline: raise TimeoutError('Renderer resize exceeded 30 minutes')
            time.sleep(.25)
    except KeyboardInterrupt: pass
    except Exception as error:
        manifest['error'] = str(error); save_manifest(); raise
    finally:
        for child in children.values():
            if child.poll() is None:
                try:os.killpg(child.pid,signal.SIGTERM)
                except ProcessLookupError:pass
        deadline=time.monotonic()+20
        for child in children.values():
            try:child.wait(timeout=max(.1,deadline-time.monotonic()))
            except subprocess.TimeoutExpired:
                try:os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                child.wait()
        children.clear();initialized.clear();save_manifest()
        for handle in handles: handle.close()


if __name__ == '__main__': main()
