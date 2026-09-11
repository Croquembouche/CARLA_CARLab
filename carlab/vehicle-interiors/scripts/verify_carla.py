"""Isolated runtime check; never changes the user's current CARLA session."""
from pathlib import Path
import json, os, queue, socket, subprocess, time
import carla

ROOT=Path('/mnt/simulations/vehicle-interiors')
PORT=2230
for p in range(PORT,PORT+3):
    with socket.socket() as sock:
        sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);sock.bind(('127.0.0.1',p))
shutdown_request=ROOT/'.test-session-2230-stop'
shutdown_request.unlink(missing_ok=True)
log=(ROOT/'runtime.log').open('w')
proc=subprocess.Popen(['/mnt/simulations/bin/carla-editor','/Game/Carla/Maps/Town10HD_Opt','-game','-RenderOffScreen',f'-graphicsadapter={os.environ.get("LINCOLN_TEST_GPU", "1")}',f'-carla-rpc-port={PORT}','-ResX=640','-ResY=360','-quality-level=Epic','-ddc=LincolnInteriorDDC','-ini:Engine:[DevOptions.Shaders]:bAllowCompilingThroughWorkers=False','-ini:Engine:[DevOptions.Shaders]:bAllowAsynchronousShaderCompiling=False','-ExecCmds=r.Streaming.PoolSize 2048,r.Shadow.Virtual.MaxPhysicalPages 2048,py /mnt/simulations/vehicle-interiors/scripts/test_session_shutdown.py','-nosound','-unattended'],stdout=log,stderr=subprocess.STDOUT)
actors=[];world=None;original=None
try:
    deadline=time.monotonic()+600
    while time.monotonic()<deadline:
        if proc.poll() is not None:raise RuntimeError('Runtime exited; see runtime.log')
        try:
            client=carla.Client('127.0.0.1',PORT);client.set_timeout(3)
            world=client.get_world();break
        except RuntimeError:time.sleep(2)
    if world is None:raise TimeoutError('CARLA did not start')
    client.set_timeout(180)
    print('CARLA world ready',flush=True)
    original=world.get_settings();settings=world.get_settings();settings.synchronous_mode=True;settings.fixed_delta_seconds=.05;world.apply_settings(settings)
    library=world.get_blueprint_library()
    variant=library.find('vehicle.lincoln.mkz_interior')
    spawn=world.get_map().get_spawn_points()[0];spawn.location.z+=.15
    vehicle=world.spawn_actor(variant,spawn);actors.append(vehicle)
    print('Lincoln variant spawned',flush=True)
    vehicle.apply_control(carla.VehicleControl(hand_brake=True))
    world.set_weather(carla.WeatherParameters(cloudiness=20,sun_altitude_angle=55,sun_azimuth_angle=170))
    bp=library.find('sensor.camera.rgb');bp.set_attribute('image_size_x','1280');bp.set_attribute('image_size_y','800');bp.set_attribute('fov','90')
    views={
        'carla_dashboard':carla.Transform(carla.Location(x=-.20,y=0,z=1.25),carla.Rotation(pitch=-16,yaw=0)),
        'carla_passenger':carla.Transform(carla.Location(x=.65,y=-.35,z=1.28),carla.Rotation(pitch=-17,yaw=134)),
        'carla_rear_seats':carla.Transform(carla.Location(x=-.43,y=.05,z=1.22),carla.Rotation(pitch=-15,yaw=180)),
    }
    latest={}
    # Capture sequentially to stay within this GPU's memory budget.
    for name,transform in views.items():
        camera=world.spawn_actor(bp,transform,attach_to=vehicle);actors.append(camera)
        q=queue.Queue(maxsize=8)
        def receive(im,q=q):
            if q.full():q.get_nowait()
            q.put_nowait(im)
        camera.listen(receive)
        for i in range(65):
            world.tick(180)
            try:
                im=q.get(timeout=5)
                while not q.empty():im=q.get_nowait()
                latest[name]=im
            except queue.Empty:pass
        assert name in latest,'Missing camera frames'
        im=latest[name]
        im.save_to_disk(str(ROOT/'renders'/(name+'.png')))
        assert len(im.raw_data)==1280*800*4
        assert max(im.raw_data[0::4])>20,'Camera is black'
        camera.stop();camera.destroy();actors.remove(camera)
    physics=vehicle.get_physics_control()
    assert len(physics.wheels)==4,'Expected four-wheel vehicle physics'
    extent=vehicle.bounding_box.extent
    print('Physics wheels:',len(physics.wheels),'Vehicle extent:',extent,flush=True)
    assert 1.5<extent.x<3.0 and .7<extent.y<1.2,'Unexpected vehicle bounds'
    runtime_text=(ROOT/'runtime.log').read_text(errors='replace')
    assert 'missing bUsedWithSkeletalMesh' not in runtime_text,'Skeletal material usage flags missing'
    assert not any('/Game/VehicleInteriors/' in line and ('Failed to compile' in line or 'missing bUsed' in line) for line in runtime_text.splitlines()),'Interior material compilation or usage failed'
    report={'result':'PASS_CAPTURE_CHECK','visual_review':'PENDING','vehicle':variant.id,'map':world.get_map().name,'cameras':list(latest),'server':client.get_server_version(),'texture_pool_mb':2048,'asynchronous_shader_compilation':False,'simultaneous_cameras':1,'wheels':len(physics.wheels),'extent_m':[extent.x,extent.y,extent.z],'note':'RGB cabin visibility. Epic scalability with bounded texture and virtual-shadow pools; no VR test.'}
    (ROOT/'runtime-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)
finally:
    for actor in (reversed(actors) if proc.poll() is None else []):
        try:
            if actor.type_id.startswith('sensor.'):actor.stop()
            actor.destroy()
        except RuntimeError:pass
    if world and original and proc.poll() is None:
        try:world.apply_settings(original)
        except RuntimeError:pass
    if proc.poll() is None:
        shutdown_request.touch()
        try:proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:proc.wait(timeout=10)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    shutdown_request.unlink(missing_ok=True)
    log.close()
    report_path=ROOT/'runtime-report.json'
    if report_path.exists():
        report=json.loads(report_path.read_text())
        report['engine_process_returncode']=proc.returncode
        report['shutdown_verified_clean']=proc.returncode in (0,-15)
        report_path.write_text(json.dumps(report,indent=2))
