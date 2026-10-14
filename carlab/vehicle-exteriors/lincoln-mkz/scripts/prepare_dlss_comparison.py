#!/usr/bin/env python3
"""Prepare hash-verified native MKZ stills for the existing CUDA evaluator."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
report = json.loads((ROOT / 'runtime-report.json').read_text())
destination = ROOT / 'dlss'
destination.mkdir(exist_ok=True)
frames = []
for vehicle, details in report['vehicles'].items():
    for view, capture in details['captures'].items():
        source = Path(capture['file'])
        frames.append(dict(
            path=str(source), sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            size=report['resolution'], camera=f'{vehicle}-{view}',
            frame=capture['frame'], timestamp=capture['timestamp'],
            map='/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio',
            vehicle=vehicle, vehicle_id=details['id'], view=view,
            transform=capture['transform'], fov=capture['fov'],
        ))
manifest = dict(
    description='Eight saved native CARLA MKZ studio RGB captures, stock and refined, without resizing or world ticks.',
    native_report=str(ROOT / 'runtime-report.json'),
    native_report_sha256=hashlib.sha256((ROOT / 'runtime-report.json').read_bytes()).hexdigest(),
    frames=frames,
)
(destination / 'inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Prepared {len(frames)} native frames in {destination / "inputs.json"}')
