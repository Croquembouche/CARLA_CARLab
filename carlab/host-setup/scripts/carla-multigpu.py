#!/usr/bin/env python3
"""Supervise a CARLA primary and one independent Vulkan renderer per GPU."""
import argparse
import json
import os
import re
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import time

import carla

ROOT = Path('/mnt/simulations/carla/carlab/host-setup')
LINUX = Path('/mnt/simulations')


def commands(gpus, port, backend, town=None):
    # Keep texture/shader cache reads off the loop-backed simulation volume.
    local_ddc = Path(os.environ.get('CARLA_DDC_PATH', str(Path.home() / '.cache' / 'carla-ddc')))
    local_ddc.mkdir(parents=True, exist_ok=True)
    shader_work = local_ddc.parent / 'carla-shader-work'
    shader_work.mkdir(parents=True, exist_ok=True)
    pso_cache = Path(os.environ.get('CARLA_VULKAN_PSO_PATH', str(Path.home() / '.cache' / 'carla-vulkan-pso')))
    persistent_pso = os.environ.get('CARLA_PERSISTENT_PSO') == '1'
    pso_options = []
    if persistent_pso:
        pso_cache.mkdir(parents=True, exist_ok=True)
        pso_options = [f'-vulkanpsocachedir={pso_cache}', '-savevulkanpsocacheonexit']
    # These unattended workers do not need a shared Unreal Insights trace server.
    # Avoid launching it concurrently from every renderer during a town restart.
    common = [str(LINUX / 'bin/carla-sim'), '-ddc=NoZenLocalFallback', f'-LocalDataCachePath={local_ddc}', '-unattended', '-nosound', '-notraceserver', '-log']
    if town:common.insert(1,town)
    primary_options = []
    physical_routing_scale = os.environ.get('CARLA_PHYSICAL_LIDAR_SCHEDULING_SCALE')
    if backend == 'gpu' and physical_routing_scale is not None:
        scale = float(physical_routing_scale)
        if not 0.01 <= scale <= 100:
            raise ValueError('Physical LiDAR scheduling scale must be between 0.01 and 100')
        primary_options.append(f'-ini:Engine:[SystemSettings]:carla.Sensors.PhysicalLidarSchedulingScale={scale:g}')
    result = [('primary', common + primary_options + [f'-carla-rpc-port={port}', '-nullrhi'])]
    # Bound the virtual-shadow cache on the 11 GiB workers. This changes
    # cache capacity, not shadow resolution or camera optical settings.
    geometry_budget = int(os.environ.get('CARLA_GEOMETRY_BUDGET_MB', '2048'))
    if not 2048 <= geometry_budget <= 8192:
        raise ValueError('Ray-tracing geometry budget must be between 2048 and 8192 MiB')
    settings = ['r.Streaming.PoolSize 2000', 'r.Shadow.Virtual.MaxPhysicalPages 2048', 'r.RayTracing.UseReferenceBasedResidency 1',
                f'r.RayTracing.NumAlwaysResidentLODs 0,r.RayTracing.ResidentGeometryMemoryPoolSizeInMB {geometry_budget}', 'carla.Sensors.GpuRayTracing ' + ('1' if backend == 'gpu' else '0')]
    if os.environ.get('CARLA_PRECACHE_LIGHT_FUNCTIONS') == '1':
        settings.append('r.RayTracing.PrecacheLightFunctionShaders 1')
    if os.environ.get('CARLA_SCOPED_MISS_SHADERS') == '1':
        settings.append('r.RayTracing.ScopedMissShaders 1')
    if os.environ.get('CARLA_REUSE_COMPLETE_PIPELINE') == '1':
        settings.append('r.RayTracing.ReuseCompletePipeline 1')
    if os.environ.get('CARLA_PIPELINE_COVERAGE_AUDIT') == '1':
        settings.append('r.RayTracing.PipelineCoverageAudit 1')
    if os.environ.get('CARLA_PER_VIEW_OCCLUSION') == '1':
        settings.append('r.Occlusion.PerViewFences 1')
    # Diagnostic mode only: completion-stage timestamps can serialize GPU work.
    # Requires the VulkanQuery fix that actually reads this console variable.
    if os.environ.get('CARLA_PRECISE_GPU_TIMING') == '1':
        settings.append('r.Vulkan.TimestampQueryStage 1')
    if os.environ.get('CARLA_SENSOR_TRACE_GPU') == '1':
        settings.append('r.GPUStatsEnabled 1')
    query_geometry_floor = int(os.environ.get('CARLA_QUERY_GEOMETRY_FLOOR_MB', '0'))
    if query_geometry_floor and not 2048 <= query_geometry_floor <= 8192:
        raise ValueError('External query geometry floor must be zero or between 2048 and 8192 MiB')
    if backend == 'gpu' and query_geometry_floor:
        settings.append(f'r.RayTracing.ExternalQueryMinimumGeometryBudgetInMB {query_geometry_floor}')
    if backend == 'gpu':
        settings += ['r.RayTracing.ExternalQueries 1', 'r.RayTracing.ExternalQueryBounds 1',
                     'r.RayTracing.ForceAllRayTracingEffects -1', 'r.RayTracing.Culling 0',
                     'r.RayTracing.Geometry.InstancedStaticMeshes.Culling 0']
    for worker, gpu in enumerate(gpus):
        cache_options = pso_options + [f'-vulkanpsocache={pso_cache / f"gpu-{gpu}.cache"}'] if persistent_pso else []
        if os.environ.get('CARLA_EARLY_CACHE_CAPS') == '1':
            # Match the existing runtime cache capacities before asset loading.
            # Device-profile initialization otherwise starts with a 4 GiB
            # texture pool and can reach the VRAM limit before ExecCmds runs.
            cache_options += ['-ForceDPCVars=r.Streaming.PoolSize=2000,r.Shadow.Virtual.MaxPhysicalPages=2048']
        # Apply before material loading; ExecCmds would be too late for eager creation.
        shader_options = ['-ini:Engine:[/Script/Engine.RendererSettings]:r.CreateShadersOnLoad=1'] if os.environ.get('CARLA_EAGER_SHADERS') == '1' else []
        if os.environ.get('CARLA_SHARED_LUMEN_CACHE') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:r.LumenScene.SceneCapture.SharedCache=1')
        if os.environ.get('CARLA_ASYNC_COMPUTE') == '1':
            shader_options += ['-ini:Engine:[SystemSettings]:r.Vulkan.AllowAsyncCompute=1',
                               '-ForceAsyncCompute']
        if os.environ.get('CARLA_COMPACT_SHADER_TABLES') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.RayTracing.CompactShaderTables=1')
        spline_cache = os.environ.get('CARLA_SPLINE_RT_CACHE', '0')
        if spline_cache in ('1', '2'):
            shader_options.append('-ini:Engine:[SystemSettings]:r.RayTracing.Geometry.SplineMeshes.PersistentGeometry=' + spline_cache)
        if os.environ.get('CARLA_LOCAL_TEXTURE_BUDGET') == '1':
            shader_options += ['-ini:Engine:[SystemSettings]:r.Vulkan.EvictionLimitPercentage=95',
                               '-ini:Engine:[SystemSettings]:r.Vulkan.EvictionLimitPercentageRenableLimit=100']
        if os.environ.get('CARLA_BINDLESS_LOCAL_DESCRIPTORS') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.Bindless.LocalDescriptors=1')
        if os.environ.get('CARLA_LOCAL_UNIFORM_UPLOADS') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.UniformUploads.DeviceLocal=1')
        if os.environ.get('CARLA_PRECACHE_LIGHT_FUNCTIONS') == '1':
            # Secondary actors register before ExecCmds are processed.
            shader_options.append('-ini:Engine:[SystemSettings]:r.RayTracing.PrecacheLightFunctionShaders=1')
        result.append((f'gpu-{gpu}', common + cache_options + shader_options + [
            # A cold town must not deliver checkerboard fallback materials while
            # its shaders compile. Keep parallel compiler workers, but finish
            # required shaders before the renderer declares initialization done.
            '-ini:Engine:[DevOptions.Shaders]:bAllowAsynchronousShaderCompiling=False',
            f'-ShaderWorkingDir={shader_work}',
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
    p.add_argument('--town', help='Installed /Game/Carla/Maps town package for primary and all renderers')
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    if args.town and not re.fullmatch(r'/Game/Carla/Maps/(?:Town[0-9]+(?:HD)?/)?Town[0-9]+(?:HD)?(?:_Opt)?', args.town):
        p.error('Invalid town package')
    pool = [3, 2, 1, 0]
    try:
        gpus = [] if args.gpus == 'auto' else [int(x) for x in args.gpus.split(',')]
        assert len(set(gpus)) == len(gpus) and all(x in pool for x in gpus)
        assert 1024 <= args.port <= 65000 - 40
    except (ValueError, AssertionError):
        p.error('Use auto or distinct GPU indices 0–3 and a valid unprivileged port.')
    # Stable per-adapter ports survive worker removal and re-creation.
    definitions = dict(commands(pool, args.port, args.backend, args.town))
    launch = [(r, definitions[r]) for r in ['primary'] + [f'gpu-{g}' for g in gpus]]
    if args.dry_run:
        print(json.dumps([{'role': r, 'command': c} for r, c in launch], indent=2)); return
    output = ROOT / 'logs' / time.strftime('multigpu-%Y%m%d-%H%M%S')
    output.mkdir(parents=True)
    children = {}; handles = []; initialized = set()
    manifest = {'town': args.town, 'backend': args.backend, 'primary_port': args.port, 'manager_pid': os.getpid(),
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
        cmd = list(definitions[role])
        trace_root = os.environ.get('CARLA_SENSOR_TRACE_DIR')
        if trace_root and role.startswith('gpu-'):
            trace_dir = Path(trace_root).expanduser().resolve() / output.name
            trace_dir.mkdir(parents=True, exist_ok=True)
            trace_channels='cpu,frame,bookmark' + (',gpu' if os.environ.get('CARLA_SENSOR_TRACE_GPU')=='1' else '')
            cmd += ['-trace='+trace_channels, '-statnamedevents',
                    '-tracefile=' + str(trace_dir / (role + '.utrace'))]
        port = int(next(x.split('=')[1] for x in cmd if x.startswith('-carla-rpc-port=')))
        for offset in range(3):
            with socket.socket() as probe:
                probe.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
                probe.bind(('0.0.0.0', port + offset))
        # Truncate logs when restarting this adapter; old readiness markers must not qualify.
        (output / (role + '.log')).write_text('')
        if os.environ.get('CARLA_NSYS_LAUNCH_ROLE') == role:
            cmd += ['-forcevulkanddrawmarkers']
            session_name = 'carla-' + output.name + '-' + role
            (output / (role + '-nsys.json')).write_text(json.dumps({'session': session_name}))
            cmd = ['/usr/local/bin/nsys', 'launch', '--session-new=' + session_name,
                   '--trace=vulkan,vulkan-annotations', '--vulkan-gpu-workload=individual', '--wait=all'] + cmd
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
        deadline = time.monotonic() + 1800
        while True:
            if children['primary'].poll() is not None: raise RuntimeError('Primary exited during startup')
            try:
                client = carla.Client('127.0.0.1', args.port); client.set_timeout(5)
                world = client.get_world(); world.get_map()
                settings = world.get_settings(); settings.synchronous_mode = True; settings.fixed_delta_seconds = .05
                world.apply_settings(settings); break
            except (OSError, RuntimeError):
                if time.monotonic() > deadline: raise TimeoutError('Primary startup exceeded 30 minutes')
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
