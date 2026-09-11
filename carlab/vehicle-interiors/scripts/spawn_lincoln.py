"""Spawn the installed Lincoln interior variant in an already running CARLA server."""
import argparse
import carla

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='127.0.0.1')
parser.add_argument('--port', type=int, default=2000)
parser.add_argument('--spawn-index', type=int, default=0)
args = parser.parse_args()
client = carla.Client(args.host, args.port)
client.set_timeout(60.0)
world = client.get_world()
blueprint = world.get_blueprint_library().find('vehicle.lincoln.mkz_interior')
blueprint.set_attribute('role_name', 'lincoln_interior')
points = world.get_map().get_spawn_points()
if not 0 <= args.spawn_index < len(points):
    raise SystemExit(f'Choose --spawn-index from 0 to {len(points)-1}')
transform = points[args.spawn_index]
transform.location.z += 0.2
vehicle = world.try_spawn_actor(blueprint, transform)
if vehicle is None:
    raise SystemExit('Spawn location is occupied. Try a different --spawn-index.')
vehicle.apply_control(carla.VehicleControl(hand_brake=True))
print(f'Spawned {vehicle.type_id}: actor {vehicle.id} at spawn {args.spawn_index}.')
print('Vehicle remains in this CARLA session after this script exits.')
print('If the world is synchronous, its existing tick owner must continue ticking.')
