#!/usr/bin/env python3
"""Test the GPU shader on engine cube assets before full CARLA map loading."""
import json
from pathlib import Path
import signal
import socket
import subprocess
import time

root = Path('/mnt/simulations/carla/carlab/host-setup')
out = Path('/mnt/simulations/verification')
for port in (2250, 2251, 2252):
    with socket.socket() as probe:
        probe.bind(('0.0.0.0', port))
report = out / 'gpu-ray-self-test.json'
if report.exists():
    report.rename(out / ('gpu-ray-self-test.previous-' + str(time.time_ns()) + '.json'))
command = ['/mnt/simulations/bin/carla-sim', '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase',
           '-carla-rpc-port=2250', '-RenderOffScreen', '-graphicsadapter=3', '-ResX=320', '-ResY=180',
           '-unattended', '-nosound', '-log',
           '-ExecCmds=r.RayTracing.UseReferenceBasedResidency 1,r.RayTracing.NumAlwaysResidentLODs 0,r.RayTracing.ResidentGeometryMemoryPoolSizeInMB 2048,carla.Sensors.GpuRayTracing 1,r.RayTracing.ExternalQueries 1,r.RayTracing.ExternalQueryBounds 1,r.RayTracing.ForceAllRayTracingEffects 0,r.RayTracing.Culling 0,r.RayTracing.Geometry.InstancedStaticMeshes.Culling 0,carla.Sensors.GpuSelfTest']
with (root / 'logs/gpu-shader-preflight.log').open('wb') as log:
    process = subprocess.Popen(command, cwd='/mnt/simulations', stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        deadline = time.monotonic() + 3600
        while not report.exists():
            if process.poll() is not None:
                raise RuntimeError(f'GPU shader preflight exited {process.returncode}; inspect gpu-shader-preflight.log')
            if time.monotonic() > deadline:
                raise TimeoutError('GPU shader preflight exceeded one hour')
            time.sleep(2)
        result = json.loads(report.read_text())
        assert result['passed']
        (out / 'gpu-ray-preflight.json').write_text(json.dumps(result, indent=2))
        print('GPU_RAY_PREFLIGHT_PASSED', json.dumps(result), flush=True)
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=40)
        except subprocess.TimeoutExpired:
            import os
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
