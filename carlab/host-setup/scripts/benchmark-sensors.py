#!/usr/bin/env python3
"""Verify frame fidelity and measure a running CARLA sensor workload."""
import argparse
import json
import math
import os
from pathlib import Path
import queue
import subprocess
import time

import carla
import numpy as np


def cpu_seconds(pids):
    total = 0
    for pid in pids:
        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        total += int(fields[11]) + int(fields[12])
    return total / os.sysconf('SC_CLK_TCK')


def servers_alive(pids):
    for pid in pids:
        try:
            if Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0] == 'Z':
                return False
        except FileNotFoundError:
            return False
    return True


def shader_compilers_active(pids):
    owners = set(pids)
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            fields = (process / 'stat').read_text().rsplit(')', 1)[1].split()
            if int(fields[1]) in owners:
                executable = (process / 'cmdline').read_bytes().split(b'\0')[0].decode()
                if Path(executable).name == 'ShaderCompileWorker':
                    return True
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return False


def gpu_snapshot():
    raw = subprocess.check_output([
        'nvidia-smi', '--query-gpu=index,utilization.gpu,memory.used,power.draw',
        '--format=csv,noheader,nounits'], text=True)
    return [dict(zip(['gpu', 'utilization_percent', 'memory_mib', 'power_w'],
                     [float(x.strip()) for x in line.split(',')])) for line in raw.splitlines()]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', type=int, default=2000)
    p.add_argument('--frames', type=int, default=100)
    p.add_argument('--warmup', type=int, default=20)
    p.add_argument('--groups', type=int, default=4)
    p.add_argument('--pids', default='', help='Comma-separated primary/worker PIDs for CPU accounting')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.frames < 1 or args.warmup < 0 or args.groups < 1:
        p.error('frames and groups must be positive; warmup must be nonnegative')
    pids = [int(x) for x in args.pids.split(',') if x]
    client = carla.Client('127.0.0.1', args.port)
    client.set_timeout(180)
    world = client.get_world()
    original = world.get_settings()
    settings = world.get_settings()
    settings.synchronous_mode = True
    settings.fixed_delta_seconds = 0.05
    settings.no_rendering_mode = False
    world.apply_settings(settings)
    actors, sensors, queues = [], [], []
    try:
        location = world.get_map().get_spawn_points()[0]
        vehicle_bp = world.get_blueprint_library().filter('vehicle.*')[0]
        vehicle = world.spawn_actor(vehicle_bp, location)
        vehicle.set_simulate_physics(False)
        actors.append(vehicle)
        specs = [
            ('sensor.camera.rgb', {'image_size_x': '1280', 'image_size_y': '720', 'sensor_tick': '0.05'}),
            ('sensor.lidar.ray_cast', {'channels': '64', 'range': '100', 'points_per_second': '1000000',
                                     'rotation_frequency': '20', 'upper_fov': '10', 'lower_fov': '-30', 'noise_stddev': '0',
                                     'dropoff_general_rate': '0', 'dropoff_intensity_limit': '0.8', 'dropoff_zero_intensity': '0', 'sensor_tick': '0.05'}),
            ('sensor.other.radar', {'range': '100', 'points_per_second': '50000', 'sensor_tick': '0.05'}),
        ]
        # Type-major subscription order assigns each sensor type once to every
        # renderer under CARLA's existing round-robin stream routing.
        for sensor_type, attributes in specs:
            for group in range(args.groups):
                bp = world.get_blueprint_library().find(sensor_type)
                for key, value in attributes.items():
                    bp.set_attribute(key, value)
                transform = carla.Transform(carla.Location(z=2.8), carla.Rotation(yaw=group * 90, pitch=-5))
                sensor = world.spawn_actor(bp, transform, attach_to=vehicle)
                actors.append(sensor)
                sensors.append((sensor_type, sensor))
                queues.append(queue.Queue())
        # Replicate actors to workers before asking them for stream tokens.
        world.tick()
        world.tick()
        for (_, sensor), inbox in zip(sensors, queues):
            sensor.listen(inbox.put)
        samples, gpu_samples = [], []
        last_rgb = None
        measurement_start = None
        cpu_start = None
        index = 0
        warmup_deadline = time.monotonic() + 1800
        while len(samples) < args.frames:
            if not servers_alive(pids):
                raise RuntimeError("A benchmark server exited; inspect its log")
            if index == args.warmup and shader_compilers_active(pids):
                if time.monotonic() > warmup_deadline:
                    raise TimeoutError('Shader workers still active after 30 minutes of warmup')
                args.warmup += 1
            if index == args.warmup:
                measurement_start = time.monotonic()
                cpu_start = cpu_seconds(pids) if pids else None
            start = time.monotonic()
            frame = world.tick()
            snapshot = world.get_snapshot()
            if snapshot.frame != frame:
                raise AssertionError(f"World snapshot {snapshot.frame} differs from tick {frame}")
            counts = []
            for (sensor_type, _), inbox in zip(sensors, queues):
                while True:
                    data = inbox.get(timeout=180)
                    if data.frame >= frame:
                        break
                if data.frame != frame:
                    raise AssertionError(f'{sensor_type}: requested frame {frame}, received {data.frame}')
                if abs(data.timestamp - snapshot.timestamp.elapsed_seconds) > 1e-5:
                    raise AssertionError(f'{sensor_type}: frame {frame} timestamp {data.timestamp:.9f} differs from primary {snapshot.timestamp.elapsed_seconds:.9f}')
                if sensor_type.endswith('rgb'):
                    if not counts:
                        last_rgb = data
                    pixels = np.frombuffer(data.raw_data, np.uint8).reshape(data.height, data.width, 4)
                    if not pixels[:, :, :3].any():
                        raise AssertionError('Black RGB output')
                    counts.append(data.width * data.height)
                else:
                    values = np.frombuffer(data.raw_data, np.float32).reshape(-1, 4)
                    if not np.isfinite(values).all() or not len(values):
                        raise AssertionError(f'{sensor_type}: non-finite or empty detections')
                    if 'lidar' in sensor_type:
                        distances = np.linalg.norm(values[:, :3], axis=1)
                        if (distances > 100.01).any():
                            raise AssertionError('LiDAR range exceeded')
                    elif (values[:, 3] < 0).any():
                        raise AssertionError('Negative radar range')
                    counts.append(len(values))
                    if index == args.warmup:
                        args.output.parent.mkdir(parents=True, exist_ok=True)
                        np.save(args.output.with_name(args.output.stem + '-' + sensor_type + '-' + str(len(counts)) + '.npy'), values)
            if index >= args.warmup:
                samples.append({'frame': frame, 'wall_seconds': time.monotonic() - start, 'counts': counts})
                if (index - args.warmup) % 10 == 0:
                    gpu_samples.append(gpu_snapshot())
            if index % 10 == 0:
                print(f'Validated frame {frame}: {index + 1}/{args.warmup + args.frames}', flush=True)
            index += 1
        elapsed = time.monotonic() - measurement_start
        cpu = cpu_seconds(pids) - cpu_start if pids else None
        report = {
            'passed': True, 'map': world.get_map().name, 'sensors': [x[0] for x in sensors],
            'shader_workers_idle_at_measurement_start': bool(pids), 'warmup_frames': args.warmup,
            'capture_timestamps_verified': True, 'frames': args.frames, 'wall_seconds': elapsed, 'frames_per_second': args.frames / elapsed,
            'server_cpu_seconds': cpu, 'average_server_cpu_cores': cpu / elapsed if cpu is not None else None,
            'cpu_seconds_per_simulated_second': cpu / (args.frames * 0.05) if cpu is not None else None,
            'pids': pids, 'gpu_samples': gpu_samples, 'samples': samples,
            'limitations': 'CPU includes all threads of the supplied processes, excludes client and shader child processes. GPU utilization is sampled. Render and collision geometry may differ.'}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if last_rgb is not None:
            last_rgb.save_to_disk(str(args.output.with_suffix('.png')))
        args.output.write_text(json.dumps(report, indent=2))
        print(json.dumps({k: v for k, v in report.items() if k not in ('samples', 'gpu_samples')}, indent=2))
    finally:
        if servers_alive(pids):
            for _, sensor in sensors:
                if sensor.is_listening:
                    sensor.stop()
            for actor in reversed(actors):
                actor.destroy()
            world.apply_settings(original)


if __name__ == '__main__':
    main()
