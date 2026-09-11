#!/usr/bin/env python3
"""Run controlled GPU geometry checks and matched CARLA sensor benchmarks."""
import argparse
import json
import os
import queue
import re
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

import carla
import numpy as np

ROOT = Path('/mnt/simulations/carla/carlab/host-setup')
LINUX = Path('/mnt/simulations')
OUT = LINUX / 'verification'


def wait_world(port, process, timeout=3600, drive_world=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f'CARLA process exited with {process.returncode}; check GPU verification logs')
        try:
            client = carla.Client('127.0.0.1', port)
            client.set_timeout(30)
            if drive_world is not None:
                drive_world.tick()
            client.get_world().get_map()
            return client
        except RuntimeError:
            time.sleep(2)
    raise TimeoutError(f'CARLA RPC {port} did not become ready')


def stop(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=40)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def benchmark(port, pids, name, groups):
    output = OUT / (name + '.json')
    command = [sys.executable, str(ROOT / 'scripts/benchmark-sensors.py'),
               '--port', str(port), '--groups', str(groups), '--warmup', '30', '--frames', '100',
               '--pids', ','.join(str(pid) for pid in pids), '--output', str(output)]
    subprocess.run(command, check=True)
    return json.loads(output.read_text())


def compare_lidar_geometry():
    comparisons = []
    # The benchmark uses 64 channels, upper/lower FOV 10/-30 degrees, and
    # round(1,000,000 * .05 / 64) == 781 horizontal samples per revolution.
    def grid(path):
        points = np.load(path)[:, :3]
        distance = np.linalg.norm(points, axis=1)
        elevation = np.degrees(np.arcsin(np.clip(points[:, 2] / distance, -1, 1)))
        azimuth = np.degrees(np.arctan2(points[:, 1], points[:, 0]))
        channel = np.rint((10 - elevation) / 40 * 63).astype(int)
        beam = np.rint(np.mod(azimuth + 180, 360) / 360 * 781).astype(int) % 781
        result = np.full((64, 781), np.nan)
        valid = (channel >= 0) & (channel < 64)
        result[channel[valid], beam[valid]] = distance[valid]
        return result
    for index in (2,):
        suffix = '-sensor.lidar.ray_cast-' + str(index) + '.npy'
        cpu = grid(OUT / ('sensors-single-cpu' + suffix))
        gpu = grid(OUT / ('sensors-single-gpu' + suffix))
        matched = np.isfinite(cpu) & np.isfinite(gpu)
        delta = np.abs(cpu[matched] - gpu[matched])
        comparisons.append({
            'sensor_index': index, 'cpu_hits': int(np.isfinite(cpu).sum()),
            'gpu_hits': int(np.isfinite(gpu).sum()), 'matched_beams': int(matched.sum()),
            'hit_miss_disagreements': int(np.logical_xor(np.isfinite(cpu), np.isfinite(gpu)).sum()),
            'absolute_depth_difference_m': {
                'median': float(np.median(delta)), 'p95': float(np.percentile(delta, 95)),
                'maximum': float(np.max(delta))} if len(delta) else None})
    return comparisons


def check_semantic_lidar(port):
    client = carla.Client('127.0.0.1', port)
    client.set_timeout(180)
    world = client.get_world()
    original = world.get_settings()
    settings = world.get_settings()
    settings.synchronous_mode = True
    settings.fixed_delta_seconds = 0.05
    world.apply_settings(settings)
    sensor = None
    try:
        pose = world.get_map().get_spawn_points()[0]
        pose.location.z += 3
        bp = world.get_blueprint_library().find('sensor.lidar.ray_cast_semantic')
        for key, value in {'channels': '32', 'points_per_second': '200000',
                           'rotation_frequency': '20', 'range': '100'}.items():
            bp.set_attribute(key, value)
        sensor = world.spawn_actor(bp, pose)
        inbox = queue.Queue()
        sensor.listen(inbox.put)
        counts = []
        for _ in range(8):
            frame = world.tick()
            data = inbox.get(timeout=180)
            assert data.frame == frame
            assert abs(data.timestamp - world.get_snapshot().timestamp.elapsed_seconds) < 1e-5
            assert len(data) > 0 and len(data.raw_data) == len(data) * 24
            points = np.frombuffer(data.raw_data, dtype=[('xyz', '<f4', (3,)),
                ('cosine', '<f4'), ('actor', '<u4'), ('tag', '<u4')])
            assert np.isfinite(points['xyz']).all() and np.isfinite(points['cosine']).all()
            assert np.max(np.linalg.norm(points['xyz'], axis=1)) <= 100.01
            counts.append(len(data))
        return {'passed': True, 'frames': 8, 'point_counts': counts}
    finally:
        if sensor is not None:
            sensor.stop()
            sensor.destroy()
        world.apply_settings(original)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    # These ports belong only to this verification invocation.
    for port in [2300, 2301, 2302] + [2400 + offset + extra for offset in (0, 10, 20, 30, 40) for extra in range(3)]:
        with socket.socket() as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(('0.0.0.0', port))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--four-only', action='store_true', help='Reuse completed single-worker results when the single-worker code path is unchanged.')
    parser.add_argument('--four-gpu-only', action='store_true', help='Reuse completed single-worker and four-worker CPU results.')
    args = parser.parse_args()
    args.four_only = args.four_only or args.four_gpu_only
    reports = {}
    if args.four_only:
        for backend in ('cpu', 'gpu'):
            name = 'sensors-single-' + backend
            reports[name] = json.loads((OUT / (name + '.json')).read_text())
            assert reports[name]['passed']
    for backend in (() if args.four_only else ('cpu', 'gpu')):
        name = 'sensors-single-' + backend
        command = [str(LINUX / 'bin/carla-sim'), '-carla-rpc-port=2300', '-graphicsadapter=3',
                   '-RenderOffScreen', '-ResX=640', '-ResY=360', '-nosound', '-unattended', '-log', '-quality-level=High']
        render_commands = 'r.Streaming.PoolSize 2000,r.RayTracing.UseReferenceBasedResidency 1,r.RayTracing.NumAlwaysResidentLODs 0,r.RayTracing.ResidentGeometryMemoryPoolSizeInMB 2048'
        if backend == 'gpu':
            old = OUT / 'gpu-ray-self-test.json'
            if old.exists():
                old.rename(OUT / ('gpu-ray-self-test.previous-' + str(time.time_ns()) + '.json'))
            render_commands += (',carla.Sensors.GpuRayTracing 1,r.RayTracing.ExternalQueries 1,r.RayTracing.ExternalQueryBounds 1,r.RayTracing.ForceAllRayTracingEffects 0,'
                           'r.RayTracing.Culling 0,r.RayTracing.Geometry.InstancedStaticMeshes.Culling 0,'
                           'carla.Sensors.GpuSelfTest')
        command.append('-ExecCmds=' + render_commands)
        with (ROOT / 'logs' / (name + '.log')).open('wb') as log:
            process = subprocess.Popen(command, cwd=LINUX, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                wait_world(2300, process)
                if backend == 'gpu':
                    deadline = time.monotonic() + 180
                    while not (OUT / 'gpu-ray-self-test.json').exists():
                        if process.poll() is not None:
                            raise RuntimeError('Controlled GPU geometry test failed; inspect sensors-single-gpu.log')
                        if time.monotonic() > deadline:
                            raise TimeoutError('Controlled GPU geometry test did not produce a success report')
                        time.sleep(1)
                    assert json.loads((OUT / 'gpu-ray-self-test.json').read_text())['passed']
                reports[name] = benchmark(2300, [process.pid], name, 1)
                if backend == 'gpu':
                    reports[name]['semantic_lidar_stream_check'] = check_semantic_lidar(2300)
            finally:
                stop(process)
    # Match both tracing backends at four workers and four sensor groups.
    if args.four_gpu_only:
        reports['sensors-four-cpu'] = json.loads((OUT / 'sensors-four-cpu.json').read_text())
        assert reports['sensors-four-cpu']['passed']
    for backend in (('gpu',) if args.four_gpu_only else ('cpu', 'gpu')):
        command = [sys.executable, str(ROOT / 'scripts/carla-multigpu.py'), '--port', '2400', '--gpus', '0,1,2,3', '--backend', backend]
        with (ROOT / 'logs/verify-four-gpu-launcher.log').open('wb') as log:
            manager = subprocess.Popen(command, cwd=LINUX, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                primary = wait_world(2400, manager)
                primary_world = primary.get_world()
                # The launcher records only the child processes it owns.
                deadline = time.monotonic() + 1800
                while True:
                    if manager.poll() is not None:
                        raise RuntimeError('Four-worker launcher exited during initialization')
                    manifests = sorted((ROOT / 'logs').glob('multigpu-*/processes.json'), key=lambda x: x.stat().st_mtime, reverse=True)
                    manifest = next((json.loads(x.read_text()) for x in manifests
                                     if json.loads(x.read_text()).get('manager_pid') == manager.pid), None)
                    if manifest and manifest.get('ready'):
                        break
                    if time.monotonic() > deadline:
                        raise TimeoutError('Four-worker launcher did not report readiness')
                    time.sleep(1)
                # Secondary servers consume replicated frames and are not independent
                # client worlds. Wait for each engine to finish initialization using
                # the owned launch logs, then advance only through the primary.
                directory = next(x.parent for x in manifests if json.loads(x.read_text()).get('manager_pid') == manager.pid)
                primary.set_timeout(180)
                print(f'FOUR_WORKERS_READY backend={backend}', flush=True)
                pids = [child['pid'] for child in manifest['children']]
                reports['sensors-four-' + backend] = benchmark(2400, pids, 'sensors-four-' + backend, 4)
                texture_pools = {}
                for child in manifest['children']:
                    if child['role'] == 'primary':
                        continue
                    values = re.findall(r'Texture pool size now (\d+) MB',
                                        (directory / (child['role'] + '.log')).read_text(errors='replace'))
                    if not values or int(values[-1]) != 2000:
                        raise AssertionError(f"Renderer texture pool was overridden: {child['role']} {values}")
                    texture_pools[child['role']] = int(values[-1])
                reports['sensors-four-' + backend]['verified_texture_pools_mib'] = texture_pools
                xml = ET.fromstring(subprocess.check_output(['nvidia-smi', '-q', '-x'], text=True))
                workers = {child['pid'] for child in manifest['children'] if child['role'].startswith('gpu-')}
                allocation = {pid: [] for pid in workers}
                for gpu_device in xml.findall('gpu'):
                    for running in gpu_device.findall('./processes/process_info'):
                        pid = int(running.findtext('pid'))
                        if pid in workers:
                            allocation[pid].append(gpu_device.attrib['id'])
                if any(len(devices) != 1 for devices in allocation.values()) or len({v[0] for v in allocation.values() if v}) != 4:
                    raise AssertionError(f'Expected four workers on four distinct GPUs, found {allocation}')
                reports['sensors-four-' + backend]['verified_worker_gpu_pci_ids'] = allocation
                if backend == 'gpu':
                    reports['sensors-four-gpu']['semantic_lidar_stream_check'] = check_semantic_lidar(2400)
                (OUT / ('sensors-four-' + backend + '.json')).write_text(json.dumps(reports['sensors-four-' + backend], indent=2))
            finally:
                stop(manager)
    cpu = reports['sensors-single-cpu']['cpu_seconds_per_simulated_second']
    gpu = reports['sensors-single-gpu']['cpu_seconds_per_simulated_second']
    result = {'functional_checks_passed': True, 'single_worker_cpu_reduction_fraction': 1 - gpu / cpu,
              'four_worker_cpu_reduction_fraction': 1 - reports['sensors-four-gpu']['cpu_seconds_per_simulated_second'] / reports['sensors-four-cpu']['cpu_seconds_per_simulated_second'],
              'workload_note': 'Single worker: one camera, lidar and radar. Four workers: four of each, distributed across GPUs. High quality with a 2000 MiB texture pool throughout.',
              'render_vs_collision_geometry': compare_lidar_geometry(),
              'geometry_equivalence_certified': False,
              'baseline_note': 'Both modes include the camera readback optimization; this comparison isolates the ray backend.',
              'reports': {name: {key: value for key, value in report.items()
                                 if key not in ('samples', 'gpu_samples')} for name, report in reports.items()}}
    (OUT / 'gpu-stack-verification.json').write_text(json.dumps(result, indent=2))
    manifest_path = ROOT / 'installation.json'
    manifest = json.loads(manifest_path.read_text())
    manifest['gpu_sensor_work'].update(status='runtime_validated', performance_measured=True,
                                       report=str(OUT / 'gpu-stack-verification.json'))
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print('GPU_STACK_VERIFIED', json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
