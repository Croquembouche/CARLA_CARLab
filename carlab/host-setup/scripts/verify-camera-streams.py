#!/usr/bin/env python3
"""Exercise all changed camera delivery paths and destruction with a copy pending."""
import argparse
import json
from pathlib import Path
import queue
import carla
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', type=int, required=True)
a = p.parse_args()
client = carla.Client('127.0.0.1', a.port)
client.set_timeout(180)
world = client.get_world()
original = world.get_settings()
settings = world.get_settings()
settings.synchronous_mode = True
settings.fixed_delta_seconds = 0.05
world.apply_settings(settings)
actors = []
streams = []
try:
    pose = world.get_map().get_spawn_points()[0]
    pose.location.z += 3
    pose.rotation.pitch = -10
    kinds = ['rgb', 'depth', 'normals', 'semantic_segmentation', 'instance_segmentation', 'optical_flow']
    for kind in kinds:
        bp = world.get_blueprint_library().find('sensor.camera.' + kind)
        bp.set_attribute('image_size_x', '320')
        bp.set_attribute('image_size_y', '180')
        sensor = world.spawn_actor(bp, pose)
        actors.append(sensor)
        inbox = queue.Queue()
        sensor.listen(inbox.put)
        streams.append((kind, sensor, inbox))
    frames = []
    for _ in range(8):
        frame = world.tick()
        snapshot = world.get_snapshot()
        assert snapshot.frame == frame
        for kind, sensor, inbox in streams:
            while True:
                data = inbox.get(timeout=180)
                if data.frame >= frame:
                    break
            assert data.frame == frame, (kind, frame, data.frame)
            assert abs(data.timestamp - snapshot.timestamp.elapsed_seconds) < 1e-5, kind
            assert data.width == 320 and data.height == 180, kind
            assert len(data.raw_data) == 320 * 180 * (8 if kind == 'optical_flow' else 4), kind
            assert data.transform.location.distance(pose.location) < 0.01, kind
            if kind == 'optical_flow':
                assert np.isfinite(np.frombuffer(data.raw_data, np.float32)).all()
        frames.append(frame)
    # Leave a new transfer outstanding, then remove the actors. Late delivery
    # must not access destroyed UObjects; subsequent RPC/ticks must still work.
    world.tick()
    for actor in reversed(actors):
        actor.stop()
        actor.destroy()
    actors.clear()
    for _ in range(4):
        world.tick()
    report = {'passed': True, 'camera_types': kinds, 'frames': frames,
              'capture_timestamps_and_poses_verified': True,
              'actor_destruction_after_unconsumed_frame_verified': True}
    Path('/mnt/simulations/verification/camera-streams.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
finally:
    for actor in reversed(actors):
        try:
            actor.stop()
            actor.destroy()
        except RuntimeError:
            pass
    world.apply_settings(original)
