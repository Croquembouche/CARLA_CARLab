"""Capture the active Lincoln from above and from the rear cabin."""
import queue
from pathlib import Path

import carla
import requests

ROOT = Path('/mnt/simulations/vehicle-interiors/renders')
BASE = 'http://127.0.0.1:8095'
HEADERS = {'X-Control-Client': 'carla-control-center'}

client = carla.Client('127.0.0.1', 2000)
client.set_timeout(60.0)
world = client.get_world()
vehicle = next(actor for actor in world.get_actors() if actor.attributes.get('role_name') == 'hero' and 'lincoln' in actor.type_id)
blueprint = world.get_blueprint_library().find('sensor.camera.rgb')
blueprint.set_attribute('image_size_x', '1280')
blueprint.set_attribute('image_size_y', '720')
blueprint.set_attribute('fov', '80')

views = {
    'current_roof_exterior': carla.Transform(carla.Location(x=-0.35, z=4.2), carla.Rotation(pitch=-90)),
    'current_roof_rear_cabin': carla.Transform(carla.Location(x=-0.45, z=0.82), carla.Rotation(pitch=72)),
    'current_roof_front_cabin': carla.Transform(carla.Location(x=0.45, z=0.82), carla.Rotation(pitch=70, yaw=180)),
}
sensors = []
try:
    requests.post(BASE + '/api/command/pause', json={}, headers=HEADERS, timeout=60).raise_for_status()
    queues = {}
    for name, transform in views.items():
        sensor = world.spawn_actor(blueprint, transform, attach_to=vehicle)
        output = queue.Queue()
        sensor.listen(output.put)
        sensors.append(sensor)
        queues[name] = output
    for _ in range(4):
        requests.post(BASE + '/api/command/step', json={}, headers=HEADERS, timeout=60).raise_for_status()
    for name, output in queues.items():
        image = output.get(timeout=30)
        image.save_to_disk(str(ROOT / f'{name}.png'))
        print(ROOT / f'{name}.png')
finally:
    for sensor in sensors:
        sensor.stop()
        sensor.destroy()
