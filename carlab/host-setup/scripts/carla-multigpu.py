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

ROOT = Path('/media/william/mist1/Simulations')
LINUX = Path('/mnt/simulations')


def commands(gpus, port, backend, town=None):
    # Keep texture/shader cache reads off the loop-backed simulation volume.
    local_ddc = Path(os.environ.get('CARLA_DDC_PATH', str(Path.home() / '.cache' / 'carla-ddc')))
    local_ddc.mkdir(parents=True, exist_ok=True)
    shader_work = local_ddc.parent / 'carla-shader-work'
    shader_work.mkdir(parents=True, exist_ok=True)
    pso_cache = Path(os.environ.get('CARLA_VULKAN_PSO_PATH', str(Path.home() / '.cache' / 'carla-vulkan-pso')))
    persistent_pso = os.environ.get('CARLA_PERSISTENT_PSO') == '1'
    rhi_threads = os.environ.get('CARLA_VULKAN_RHI_THREADS')
    if rhi_threads is not None and rhi_threads not in ('1', '2'):
        raise ValueError('Vulkan RHI threads must be 1 (single) or 2 (parallel)')
    pso_options = []
    if persistent_pso:
        pso_cache.mkdir(parents=True, exist_ok=True)
        pso_options = [f'-vulkanpsocachedir={pso_cache}', '-savevulkanpsocacheonexit']
    # These unattended workers do not need a shared Unreal Insights trace server.
    # Avoid launching it concurrently from every renderer during a town restart.
    common = [str(LINUX / 'bin/carla-sim'), '-ddc=NoZenLocalFallback', f'-LocalDataCachePath={local_ddc}', '-unattended', '-nosound', '-notraceserver', '-log']
    # Opt-in scheduling experiment: bound each independent engine's worker
    # pool without changing quality, sensor settings or OS CPU affinity.
    core_limit = int(os.environ.get('CARLA_ENGINE_CORE_LIMIT', '0'))
    if core_limit and not 4 <= core_limit <= 64:
        raise ValueError('Engine core limit must be zero or between 4 and 64')
    core_scope = os.environ.get('CARLA_ENGINE_CORE_LIMIT_GPUS')
    core_gpus = None
    if core_scope is not None:
        if not re.fullmatch(r'[0-9]+(?:,[0-9]+)*', core_scope):
            raise ValueError('Engine core GPU scope must be a comma-separated list of device indices')
        indices = [int(value) for value in core_scope.split(',')]
        if len(set(indices)) != len(indices) or any(value > 63 for value in indices):
            raise ValueError('Engine core GPU scope must contain unique indices between 0 and 63')
        core_gpus = set(indices)
    if core_limit and core_gpus is None:
        common.append(f'-corelimit={core_limit}')
    if os.environ.get('CARLA_NVME_VT_CACHE') == '1':
        vt_cache = Path(os.environ.get('CARLA_VT_CACHE_PATH', str(Path.home() / '.cache' / 'carla-vt'))).expanduser().absolute()
        vt_cache.mkdir(parents=True, exist_ok=True)
        # Virtual texture chunks have a separate disk cache from the main DDC.
        common.append(f'-ini:Engine:[VirtualTextureChunkDDCCache]:Path={vt_cache}')
    if town:common.insert(1,town)
    primary_options = []
    if os.environ.get('CARLA_LAZY_ACTOR_CLASSES') == '1':
        # Preserve eager Blueprint initialization on the authoritative physics
        # server. Deferred first-use vehicles need a separate physics fix;
        # rendering replicas disable vehicle physics and can defer safely.
        primary_options.append('-ini:Engine:[SystemSettings]:carla.ActorDefinitions.LazyClasses=0')
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
    if os.environ.get('CARLA_MOTION_QUERY_BOUNDS') == '1':
        settings.append('carla.Sensors.MotionQueryBounds 1')
    if os.environ.get('CARLA_PIPELINE_COVERAGE_AUDIT') == '1':
        settings.append('r.RayTracing.PipelineCoverageAudit 1')
    if os.environ.get('CARLA_PER_VIEW_OCCLUSION') == '1':
        settings.append('r.Occlusion.PerViewFences 1')
    # Diagnostic mode only: completion-stage timestamps can serialize GPU work.
    # Requires the VulkanQuery fix that actually reads this console variable.
    if os.environ.get('CARLA_PRECISE_GPU_TIMING') == '1':
        settings.append('r.Vulkan.TimestampQueryStage 1')
    retain_shadow_cache = os.environ.get('CARLA_RETAIN_VSM_CACHE')
    if retain_shadow_cache is not None:
        if retain_shadow_cache not in ('0', '1'):
            raise ValueError('VSM cache retention must be 0 or 1')
        settings.append('r.Shadow.Virtual.RetainCacheForDisabledViews ' + retain_shadow_cache)
    retention_scope = os.environ.get('CARLA_RETAIN_VSM_CACHE_GPUS')
    retention_gpus = None
    if retention_scope is not None:
        if not re.fullmatch(r'[0-9]+(?:,[0-9]+)*', retention_scope):
            raise ValueError('VSM retention GPU scope must be a comma-separated list of device indices')
        indices = [int(value) for value in retention_scope.split(',')]
        if len(set(indices)) != len(indices) or any(value > 63 for value in indices):
            raise ValueError('VSM retention GPU scope must contain unique indices between 0 and 63')
        retention_gpus = set(indices)
    gpu_stats = os.environ.get('CARLA_SENSOR_TRACE_GPU')
    if gpu_stats is not None:
        if gpu_stats not in ('0', '1'):
            raise ValueError('GPU statistics must be 0 or 1')
        settings.append('r.GPUStatsEnabled ' + gpu_stats)
    barrier_inline_scratch = os.environ.get('CARLA_VULKAN_BARRIER_INLINE_SCRATCH', '0')
    if barrier_inline_scratch not in ('0', '1'):
        raise ValueError('CARLA_VULKAN_BARRIER_INLINE_SCRATCH must be 0 or 1')
    settings.append('r.Vulkan.BarrierInlineScratch ' + barrier_inline_scratch)
    mesh_batch_size = os.environ.get('CARLA_RT_MESH_BATCH_SIZE', '1024')
    if mesh_batch_size not in ('128', '1024'):
        raise ValueError('CARLA_RT_MESH_BATCH_SIZE must be 128 or 1024')
    settings.append('r.RayTracing.ParallelMeshBatchSize ' + mesh_batch_size)
    copy_timing = os.environ.get('CARLA_CAMERA_COPY_TIMING', '1')
    if copy_timing not in ('0', '1'):
        raise ValueError('CARLA_CAMERA_COPY_TIMING must be 0 or 1')
    settings.append('carla.Sensors.CameraCopyTiming ' + copy_timing)
    exact_palette_reuse = os.environ.get('CARLA_EXACT_PALETTE_REUSE', '0')
    if exact_palette_reuse not in ('0', '1'):
        raise ValueError('Exact skin palette reuse must be 0 or 1')
    settings += ['r.SkinCache.ExactPaletteReuse ' + exact_palette_reuse,
                 'r.SkinCache.PaletteAudit 0', 'r.SkinCache.ExactPaletteReuseAudit 0', 'r.SkinCache.ExactPaletteVerify 0', 'r.SkinCache.ExactPaletteFixture 0']
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
        worker_common = common
        if core_limit and core_gpus is not None and gpu in core_gpus:
            worker_common = common + [f'-corelimit={core_limit}']
        cache_options = pso_options + [f'-vulkanpsocache={pso_cache / f"gpu-{gpu}.cache"}'] if persistent_pso else []
        if os.environ.get('CARLA_EARLY_CACHE_CAPS') == '1':
            # Match the existing runtime cache capacities before asset loading.
            # Device-profile initialization otherwise starts with a 4 GiB
            # texture pool and can reach the VRAM limit before ExecCmds runs.
            cache_options += ['-ForceDPCVars=r.Streaming.PoolSize=2000,r.Shadow.Virtual.MaxPhysicalPages=2048']
        # Apply before material loading; ExecCmds would be too late for eager creation.
        shader_options = ['-ini:Engine:[/Script/Engine.RendererSettings]:r.CreateShadersOnLoad=1'] if os.environ.get('CARLA_EAGER_SHADERS') == '1' else []
        if os.environ.get('CARLA_LAZY_ACTOR_CLASSES') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:carla.ActorDefinitions.LazyClasses=1')
        if os.environ.get('CARLA_EARLY_CAMERA_SUBMIT') == '1' and gpu != 0:
            # The roof LiDAR stays on GPU0. Submit camera-only workers earlier
            # while preserving their asynchronous readback and completion fence.
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.EarlyCameraSubmit=1')
        for env_key, cvar in [('CARLA_LUMEN_MIXED_HIT_LIGHTING', 'r.Lumen.Reflections.MixedQueueHitLighting'),
                              ('CARLA_LUMEN_REFLECTIONS_ASYNC', 'r.Lumen.Reflections.AsyncCompute')]:
            value = os.environ.get(env_key)
            if value is not None:
                if value not in ('0', '1'):
                    raise ValueError(env_key + ' must be 0 or 1')
                if gpu != 0:
                    shader_options.append('-ini:Engine:[SystemSettings]:' + cvar + '=' + value)
        if os.environ.get('CARLA_FULL_MATERIAL_SHADER_MAPS') == '1':
            # UE5's job-DDC mode skips material-map preparation and relies on
            # on-demand compilation. Uncooked game workers must prepare full
            # maps before rendering legacy non-Nanite town geometry.
            shader_options.append('-ini:Engine:[SystemSettings]:r.ShaderCompiler.JobCacheDDC=0')
        if os.environ.get('CARLA_CAMERA_VIEWPORT_UNLIT') == '1' and gpu != 0:
            # Only the local maintenance viewport changes. SceneCapture flags
            # retain their ray-traced lighting; roof GPU0 keeps its lit view.
            shader_options.append('-ini:Engine:[SystemSettings]:r.GameViewport.Unlit=1')
        lidar_viewport_unlit = os.environ.get('CARLA_LIDAR_VIEWPORT_UNLIT')
        if lidar_viewport_unlit is not None:
            if lidar_viewport_unlit not in ('0', '1'):
                raise ValueError('LiDAR worker viewport lighting mode must be 0 or 1')
            if gpu == 0:
                # Keep the off-screen main view and its external ray-query
                # callback. This changes only its lighting; sensor camera
                # families and physical LiDAR optics remain independent.
                shader_options.append('-ini:Engine:[SystemSettings]:r.GameViewport.Unlit=' + lidar_viewport_unlit)
        skip_viewport = os.environ.get('CARLA_CAMERA_SKIP_VIEWPORT')
        if skip_viewport is not None:
            if skip_viewport not in ('0', '1'):
                raise ValueError('Camera worker viewport skipping must be 0 or 1')
            if gpu != 0:
                # The roof's main view still supplies the external physical query
                # scene. Only camera workers omit their unused local viewport.
                shader_options.append('-ini:Engine:[SystemSettings]:r.GameViewport.SkipWorldRendering=' + skip_viewport)
        if os.environ.get('CARLA_CACHE_MATERIAL_PRESETS') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.CacheMaterialPresets=1')
        for env_key, cvar in [('CARLA_CAMERA_RENDER_GROUP', 'carla.Sensors.CameraRenderGroup'),
                              ('CARLA_CAMERA_SCENE_UPDATE_ONCE', 'r.RayTracing.SceneUpdateOnce')]:
            value = os.environ.get(env_key)
            if value is not None:
                if value not in ('0', '1'):
                    raise ValueError(env_key + ' must be 0 or 1')
                if gpu != 0:
                    shader_options.append('-ini:Engine:[SystemSettings]:' + cvar + '=' + value)
        camera_pair = os.environ.get('CARLA_CAMERA_CAPTURE_PAIR')
        if camera_pair is not None:
            if camera_pair not in ('0', '1'):
                raise ValueError('Camera capture pairing must be 0 or 1')
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.CameraCapturePair=' + camera_pair)
        skip_unrendered = os.environ.get('CARLA_SKIP_UNRENDERED_SNAPSHOT')
        if skip_unrendered is not None:
            if skip_unrendered not in ('0', '1'):
                raise ValueError('Unrendered snapshot skipping must be 0 or 1')
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.SkipUnrenderedSnapshotComponents=' + skip_unrendered)
        if os.environ.get('CARLA_REUSE_RECEIVER_SCRATCH') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.ReuseReceiverScratch=1')
        if os.environ.get('CARLA_APPEND_MOTION_GEOMETRY') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.AppendMotionGeometry=1')
        if os.environ.get('CARLA_PARALLEL_RAY_VALIDATION') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.ParallelRayValidation=1')
        if os.environ.get('CARLA_REUSE_PACKED_RAY_INPUTS') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:carla.Sensors.ReusePackedRayInputs=1')
        host_record_stride = os.environ.get('CARLA_HOST_SHADER_RECORD_STRIDE')
        if host_record_stride is not None:
            if host_record_stride not in ('512', '1024', '2048', '4096'):
                raise ValueError('Host shader record stride must be 512, 1024, 2048 or 4096 bytes')
            # GPU tables remain compacted to the actual written record size.
            # Keep the native write bounds checks for every local parameter.
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.RayTracing.MaxShaderGroupStride=' + host_record_stride)
        host_record_pool = os.environ.get('CARLA_HOST_SHADER_RECORD_POOL_MB', '0')
        if host_record_pool.isdecimal() and 0 < int(host_record_pool) <= 256:
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.RayTracing.HostRecordPoolMB=' + host_record_pool)
        if os.environ.get('CARLA_EARLY_GEOMETRY_RESIDENCY') == '1':
            # ExecCmds runs after town resources load. Starting residency earlier
            # avoids creating every town BLAS before the query/capture references
            # are known; referenced meshes retain their existing LOD and shading.
            shader_options += ['-ini:Engine:[SystemSettings]:r.RayTracing.UseReferenceBasedResidency=1',
                               '-ini:Engine:[SystemSettings]:r.RayTracing.NumAlwaysResidentLODs=0',
                               f'-ini:Engine:[SystemSettings]:r.RayTracing.ResidentGeometryMemoryPoolSizeInMB={geometry_budget}']
        if backend == 'gpu' and os.environ.get('CARLA_EARLY_QUERY_BOUNDS') == '1':
            # Before the first sensor subscribes, the maintenance view has no
            # query or camera lighting volume. Match the eventual query policy
            # before map startup so it cannot populate an unrelated RT scene.
            shader_options += ['-ini:Engine:[SystemSettings]:r.RayTracing.ExternalQueries=1',
                               '-ini:Engine:[SystemSettings]:r.RayTracing.ExternalQueryBounds=1']
        if rhi_threads is not None:
            # Read during Vulkan device initialization, before parallel command
            # translation is enabled. Camera and LiDAR fidelity are unchanged.
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.RHIThread=' + rhi_threads)
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
        if os.environ.get('CARLA_REUSE_FALLBACK_BUFFER_POOLS') == '1':
            shader_options.append('-ini:Engine:[SystemSettings]:r.Vulkan.ReuseFallbackBufferPools=1')
        if os.environ.get('CARLA_PRECACHE_LIGHT_FUNCTIONS') == '1':
            # Secondary actors register before ExecCmds are processed.
            shader_options.append('-ini:Engine:[SystemSettings]:r.RayTracing.PrecacheLightFunctionShaders=1')
        worker_settings = settings
        if retention_gpus is not None and gpu not in retention_gpus:
            worker_settings = [
                'r.Shadow.Virtual.RetainCacheForDisabledViews 0'
                if value.startswith('r.Shadow.Virtual.RetainCacheForDisabledViews ') else value
                for value in settings]
        result.append((f'gpu-{gpu}', worker_common + cache_options + shader_options + [
            # A cold town must not deliver checkerboard fallback materials while
            # its shaders compile. Keep parallel compiler workers, but finish
            # required shaders before the renderer declares initialization done.
            '-ini:Engine:[DevOptions.Shaders]:bAllowAsynchronousShaderCompiling=False',
            f'-ShaderWorkingDir={shader_work}',
            f'-carla-rpc-port={port + 10 * (worker + 1)}',
            '-carla-primary-host=127.0.0.1', f'-carla-primary-port={port + 2}',
            f'-graphicsadapter={gpu}', '-RenderOffScreen', '-ResX=160', '-ResY=90',
            '-quality-level=High', '-ExecCmds=' + ','.join(worker_settings)]))
    return result



def ordered_workers(gpus):
    value=os.environ.get('CARLA_WORKER_CONNECT_ORDER')
    if value is None:return list(gpus)
    try:order=[int(x) for x in value.split(',')]
    except ValueError:raise ValueError('Worker connection order must be a permutation of 0,1,2,3')
    if len(order)!=4 or set(order)!={0,1,2,3}:
        raise ValueError('Worker connection order must be a permutation of 0,1,2,3')
    return [gpu for gpu in order if gpu in gpus]


def start_workers_in_order(gpus, start, ready, connected, ordered=True, sleep=time.sleep):
    """Wait for the real primary connection before starting the next tied worker."""
    for gpu in gpus:
        start(gpu)
        if ordered:
            while not ready(gpu):
                sleep(1)
            connected(gpu)
            sleep(.25)


def worker_connected(pid,primary_port,proc_root=Path('/proc')):
    # Inspect only this child and its descendants (Nsight can be its parent).
    # A listening RPC port does not prove that its multigpu client connected.
    pending=[pid];seen=set();inodes=set()
    while pending:
        current=pending.pop()
        if current in seen:continue
        seen.add(current)
        process=proc_root/str(current)
        # Nsight can fork UE from a helper thread, not its main thread.
        for children_file in (process/'task').glob('*/children'):
            try:pending.extend(int(x) for x in children_file.read_text().split())
            except (OSError,ValueError):pass
        try:
            for fd in (process/'fd').iterdir():
                try:target=fd.readlink().as_posix()
                except OSError:continue
                if target.startswith('socket:['):inodes.add(target[8:-1])
        except OSError:pass
    try:connections=(proc_root/str(pid)/'net/tcp').read_text().splitlines()[1:]
    except OSError:return False
    for line in connections:
        fields=line.split()
        if len(fields)>9 and fields[3]=='01' and fields[9] in inodes:
            if int(fields[2].rsplit(':',1)[1],16)==primary_port:return True
    return False


def renderer_pid(launcher_pid, command, proc_root=Path('/proc')):
    """Resolve exactly one UE renderer within a profiler's owned process tree.

    The launcher PID remains the process handle used for waiting. Provenance
    and native-first shutdown need the renderer PID rather than nsys.
    Startup shells may not have exec'd UE yet; return None until they do.
    """
    expected = [arg for arg in command if arg.startswith(('-carla-rpc-port=', '-graphicsadapter='))]
    if not any(arg.startswith('-carla-rpc-port=') for arg in expected):
        raise ValueError('Renderer discovery requires its exact RPC port')
    pending = [launcher_pid]; seen = set(); matches = []
    while pending:
        pid = pending.pop()
        if pid in seen: continue
        seen.add(pid)
        if len(seen) > 64: raise RuntimeError('Profiler process tree exceeds discovery bound')
        process = proc_root / str(pid)
        try:
            args = (process / 'cmdline').read_bytes().decode('utf8').split('\0')
            if (process / 'exe').readlink().name == 'UnrealEditor' and all(arg in args for arg in expected):
                matches.append(pid)
        except (OSError, UnicodeError): pass
        # A helper thread can fork the target; inspect every thread's children.
        for children_file in (process / 'task').glob('*/children'):
            try: pending.extend(int(value) for value in children_file.read_text().split())
            except (OSError, ValueError): pass
    if len(matches) > 1: raise RuntimeError('Ambiguous native renderer within profiler process tree')
    return matches[0] if matches else None


def signal_renderer(child, signum):
    # Nsight puts UE in a separate process group. Signal that identified
    # native process too; signalling only the wrapper group can leave it alive.
    if child.args[0] == '/usr/local/bin/nsys':
        actual_pid = renderer_pid(child.pid, child.args)
        if actual_pid is None:
            # The wrapper may exit while UE is still draining. Retain the
            # verified renderer identity, but never signal a reused PID.
            identity = getattr(child, '_carla_renderer_identity', None)
            if identity is not None:
                candidate, start_ticks = identity
                try:
                    current_ticks = (Path('/proc')/str(candidate)/'stat').read_text().rsplit(')',1)[1].split()[19]
                    if current_ticks == start_ticks and renderer_pid(candidate,child.args) == candidate:
                        actual_pid = candidate
                except OSError: pass
        if actual_pid is not None and actual_pid != child.pid:
            try: os.kill(actual_pid, signum)
            except ProcessLookupError: pass
            # Let UE drain and the waiting Nsight launcher exit naturally.
            # TERM on the wrapper can forcibly terminate its target before
            # UE writes its clean exit. Timeout escalation still kills both.
            if signum == signal.SIGTERM: return
    try: os.killpg(child.pid, signum)
    except ProcessLookupError: pass


class ExistingRenderer:
    """Monitor an identified renderer when replacing only its supervisor."""
    def __init__(self, row):
        self.pid, self.args = row['pid'], row['command']
        raw = (Path('/proc')/str(self.pid)/'stat').read_text().rsplit(')', 1)[1].split()
        self.start_ticks = raw[19]
        command = (Path('/proc')/str(self.pid)/'cmdline').read_bytes().decode().split('\0')
        expected = next(a for a in self.args if a.startswith('-carla-rpc-port='))
        if expected not in command or (Path('/proc')/str(self.pid)/'exe').readlink().name != 'UnrealEditor':
            raise RuntimeError('Refusing to adopt an unrelated renderer process')
        self.returncode = None

    def poll(self):
        try:
            raw = (Path('/proc')/str(self.pid)/'stat').read_text().rsplit(')', 1)[1].split()
            if raw[19] != self.start_ticks or raw[0] == 'Z': self.returncode = 1
        except OSError: self.returncode = 1
        return self.returncode

    def wait(self, timeout=None):
        started = time.monotonic()
        while self.poll() is None:
            if timeout is not None and time.monotonic()-started >= timeout:
                raise subprocess.TimeoutExpired(self.args, timeout)
            time.sleep(.1)
        return self.returncode


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gpus', default='auto', help='auto starts without renderers; or comma-separated Vulkan adapters')
    p.add_argument('--port', type=int, default=2000)
    p.add_argument('--backend', choices=['gpu', 'cpu'], default='gpu')
    p.add_argument('--town', help='Installed /Game/Carla/Maps town package for primary and all renderers')
    p.add_argument('--adopt-manifest', help='Replace the supervisor of an existing identified renderer pool')
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    if args.town and not re.fullmatch(r'/Game/Carla/Maps/(?:Town[0-9]+(?:HD)?/)?Town[0-9]+(?:HD)?(?:_Opt)?', args.town):
        p.error('Invalid town package')
    pool = [3, 2, 1, 0]
    try:
        gpus = [] if args.gpus == 'auto' else [int(x) for x in args.gpus.split(',')]
        assert len(set(gpus)) == len(gpus) and all(x in pool for x in gpus)
        gpus = ordered_workers(gpus)
        assert 1024 <= args.port <= 65000 - 40
    except (ValueError, AssertionError):
        p.error('Use auto or distinct GPU indices 0–3 and a valid unprivileged port.')
    # Stable per-adapter ports survive worker removal and re-creation.
    definitions = dict(commands(pool, args.port, args.backend, args.town))
    launch = [(r, definitions[r]) for r in ['primary'] + [f'gpu-{g}' for g in gpus]]
    if args.dry_run:
        print(json.dumps([{'role': r, 'command': c} for r, c in launch], indent=2)); return
    output = Path(args.adopt_manifest).resolve().parent if args.adopt_manifest else ROOT / 'logs' / time.strftime('multigpu-%Y%m%d-%H%M%S')
    output.mkdir(parents=True, exist_ok=bool(args.adopt_manifest))
    children = {}; handles = []; initialized = set()
    manifest = {'town': args.town, 'backend': args.backend, 'primary_port': args.port, 'manager_pid': os.getpid(),
                'ready': False, 'connection_order': [], 'pool': pool, 'requested_gpus': gpus, 'request_id': None, 'error': None}

    if args.adopt_manifest:
        previous = json.loads(Path(args.adopt_manifest).read_text())
        if previous['primary_port'] != args.port or previous['town'] != args.town:
            raise RuntimeError('Existing renderer pool does not match the requested server')
        children = {row['role']: ExistingRenderer(row) for row in previous['children']}
        initialized = {row['role'] for row in previous['children'] if row.get('initialized')}
        manifest = {**previous, 'manager_pid': os.getpid(), 'error': None}

    saved_manifest = None

    def save_manifest():
        nonlocal saved_manifest
        rows = []
        for role, child in children.items():
            row = {'role': role, 'pid': child.pid, 'command': child.args,
                   'initialized': role in initialized}
            if child.args[0] == '/usr/local/bin/nsys':
                actual_pid = renderer_pid(child.pid, child.args)
                if actual_pid is not None:
                    child._carla_renderer_identity = (actual_pid, (Path('/proc')/str(actual_pid)/'stat').read_text().rsplit(')',1)[1].split()[19])
                if role in initialized and actual_pid is None:
                    raise RuntimeError('Initialized profiled worker has no identifiable UE renderer: ' + role)
                row.update(pid=actual_pid if actual_pid is not None else child.pid,
                           launcher_pid=child.pid, renderer_pid_pending=actual_pid is None)
            rows.append(row)
        manifest['children'] = rows
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
            workload = os.environ.get('CARLA_NSYS_GPU_WORKLOAD', 'individual')
            if workload not in ('individual', 'batch', 'none'):
                raise ValueError('Nsight Vulkan GPU workload must be individual, batch or none')
            session_name = 'carla-' + output.name + '-' + role
            (output / (role + '-nsys.json')).write_text(json.dumps({'session': session_name, 'gpu_workload': workload}))
            cmd = ['/usr/local/bin/nsys', 'launch', '--session-new=' + session_name,
                   '--trace=vulkan,vulkan-annotations', '--vulkan-gpu-workload=' + workload, '--wait=all'] + cmd
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
                signal_renderer(child,signal.SIGTERM)
        deadline=time.monotonic()+20
        for role in roles:
            child=children[role]
            try:child.wait(timeout=max(.1,deadline-time.monotonic()))
            except subprocess.TimeoutExpired:
                signal_renderer(child,signal.SIGKILL)
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

    def start_worker(gpu):
        start_child(f'gpu-{gpu}')

    def worker_ready(gpu):
        inspect()
        role = f'gpu-{gpu}'
        return role in initialized and worker_connected(children[role].pid, args.port + 2)

    def record_worker_connection(gpu):
        manifest['connection_order'].append(f'gpu-{gpu}')
        save_manifest()

    def start_new_workers(gpus):
        start_workers_in_order(gpus, start_worker, worker_ready, record_worker_connection,
                               ordered=os.environ.get('CARLA_WORKER_CONNECT_ORDER') is not None)

    # Shader preparation may take arbitrarily long; process failures still abort.
    try:
        if not args.adopt_manifest:
            start_child('primary')
            while True:
                if children['primary'].poll() is not None: raise RuntimeError('Primary exited during startup')
                try:
                    client = carla.Client('127.0.0.1', args.port); client.set_timeout(5)
                    world = client.get_world(); world.get_map()
                    settings = world.get_settings(); settings.synchronous_mode = True; settings.fixed_delta_seconds = .05
                    world.apply_settings(settings); break
                except (OSError, RuntimeError):
                    time.sleep(1)
            start_new_workers(gpus)

            while not inspect():
                time.sleep(1)
            client.set_timeout(180)
            for _ in range(10): world.tick(); time.sleep(.1)
            manifest['ready'] = True; save_manifest()
            print('READY: renderer pool accepts paused loadout changes.', flush=True)
        else:
            save_manifest()
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
                    # Starting in sequence alone does not establish connection
                    # order: a slower-loading worker can otherwise join last.
                    start_new_workers([gpu for gpu in ordered_workers(desired)
                                       if f'gpu-{gpu}' not in children])
            if manifest.get('resizing'):
                if inspect(): manifest['resizing'] = False; save_manifest()
            time.sleep(.25)
    except KeyboardInterrupt: pass
    except Exception as error:
        manifest['error'] = str(error); save_manifest(); raise
    finally:
        for child in children.values():
            if child.poll() is None:
                signal_renderer(child,signal.SIGTERM)
        deadline=time.monotonic()+20
        for child in children.values():
            try:child.wait(timeout=max(.1,deadline-time.monotonic()))
            except subprocess.TimeoutExpired:
                signal_renderer(child,signal.SIGKILL)
                child.wait()
        children.clear();initialized.clear();save_manifest()
        for handle in handles: handle.close()


if __name__ == '__main__': main()
