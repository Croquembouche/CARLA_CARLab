"""Launch this installation on an isolated port and verify RPC, ticks and RGB."""
from pathlib import Path
import json, os, queue, socket, subprocess, time, sys, shutil
import carla
root=Path('/mnt/simulations/carla/carlab/host-setup')
linux=Path('/mnt/simulations')
(root/'logs').mkdir(parents=True, exist_ok=True)
# CARLA's custom Unreal targets are not part of CMake's default 'all'.
# The installed carla-sim command uses UnrealEditor -game with source assets.
# Build that application module explicitly before launching it.
if '--skip-build' not in sys.argv:
    (root/'logs/build-status.txt').write_text(time.strftime('%Y-%m-%dT%H:%M:%S%z') + ' Compiling CARLA Unreal editor and simulator module\n')
    subprocess.run([str(linux/'venvs/carla/bin/cmake') if (linux/'venvs/carla/bin/cmake').exists() else shutil.which('cmake'), '--build', '/mnt/simulations/carla/Build',
                    '--target', 'carla-unreal-configure'], check=True)
    subprocess.run(['/mnt/simulations/UnrealEngine5_carla/Engine/Build/BatchFiles/Linux/Build.sh',
                    'CarlaUnrealEditor', 'Linux', 'Development',
                    '-project=/mnt/simulations/carla/Unreal/CarlaUnreal/CarlaUnreal.uproject', '-buildscw'], check=True)
(root/'logs/build-status.txt').write_text(time.strftime('%Y-%m-%dT%H:%M:%S%z') + ' CARLA Unreal editor/simulator built; verifying RPC, ticks and RGB camera\n')
out=linux/'verification'
out.mkdir(parents=True, exist_ok=True)
port=2200
for candidate in (port,port+1,port+2):
    with socket.socket() as s:
        s.bind(('127.0.0.1',candidate))
gpu_rows=subprocess.check_output(['nvidia-smi','--query-gpu=index,memory.free','--format=csv,noheader,nounits'],text=True).splitlines()
gpus=[tuple(map(int,row.split(','))) for row in gpu_rows]
gpu=max(gpus,key=lambda item:(item[1],item[0]!=0,-item[0]))[0]
env=os.environ.copy()
env['UE_LocalDataCachePath']=str(linux/'cache/unreal')
log=(root/'logs/carla-runtime.log').open('w')
process=subprocess.Popen([str(linux/'bin/carla-sim'),'-RenderOffScreen','-quality-level=Low',f'-carla-port={port}',f'-graphicsadapter={gpu}','-ResX=640','-ResY=360','-nosound','-unattended','-log'],env=env,stdout=log,stderr=subprocess.STDOUT)
actors=[]
world=None
original=None
try:
    deadline=time.monotonic()+3600
    while time.monotonic()<deadline:
        if process.poll() is not None:
            raise RuntimeError(f'CARLA exited {process.returncode}; see carla-runtime.log')
        try:
            client=carla.Client('127.0.0.1',port)
            client.set_timeout(10.0)
            world=client.get_world()
            break
        except RuntimeError:
            time.sleep(5)
    if world is None: raise TimeoutError('CARLA startup exceeded one hour')
    client.set_timeout(180.0)
    original=world.get_settings()
    settings=world.get_settings()
    settings.synchronous_mode=True
    settings.fixed_delta_seconds=0.05
    world.apply_settings(settings)
    blueprints=world.get_blueprint_library()
    spawn=world.get_map().get_spawn_points()[0]
    vehicle=world.spawn_actor(blueprints.filter('vehicle.*')[0],spawn)
    actors.append(vehicle)
    camera_bp=blueprints.find('sensor.camera.rgb')
    camera_bp.set_attribute('image_size_x','320')
    camera_bp.set_attribute('image_size_y','180')
    camera=world.spawn_actor(camera_bp,carla.Transform(carla.Location(x=1.5,z=2.4)),attach_to=vehicle)
    actors.append(camera)
    images=queue.Queue()
    camera.listen(images.put)
    client.set_timeout(120.0)
    frames=[]
    image=None
    for _ in range(60):
        frames.append(world.tick(120.0))
        try:
            candidate=images.get(timeout=10)
            while not images.empty():
                candidate=images.get_nowait()
            if max(candidate.raw_data[0::4])>0:
                image=candidate
                if len(frames)>=8:
                    break
        except queue.Empty:
            pass
    assert image is not None, 'Camera produced no non-black image after 60 ticks'
    image.save_to_disk(str(out/'carla-rgb.png'))
    assert len(image.raw_data)==320*180*4
    report={'result':'PASS','gpu':gpu,'client':client.get_client_version(),'server':client.get_server_version(),'map':world.get_map().name,'blueprints':len(blueprints),'ticks':frames,'camera':[image.width,image.height]}
    (out/'carla-runtime.json').write_text(json.dumps(report,indent=2)+'\n')
    print('CARLA_RUNTIME_VERIFIED',json.dumps(report))
finally:
    for actor in reversed(actors):
        try:
            if actor.type_id.startswith('sensor.'): actor.stop()
            actor.destroy()
        except RuntimeError: pass
    if world is not None and original is not None:
        try: world.apply_settings(original)
        except RuntimeError: pass
    process.terminate()
    try: process.wait(timeout=60)
    except subprocess.TimeoutExpired:
        process.kill();process.wait()
    log.close()

# Full multi-GPU benchmarks are separate workloads; this is an isolated RGB smoke test.
