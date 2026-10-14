"""Own only a fresh server on port 2250, with one sequential RGB sensor."""
import carla,json,time,queue,subprocess,socket,math,os
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');PORT=2250
for p in range(PORT,PORT+3):
    with socket.socket()as s:
        s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('127.0.0.1',p))
stop=R/'.stop-2250';stop.unlink(missing_ok=True);log=(R/'native-console.log').open('w')
command=['/mnt/simulations/bin/carla-editor','/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio','-game','-RenderOffScreen','-graphicsadapter=0','-SkipVulkanProfileCheck','-carla-rpc-port=2250','-ResX=160','-ResY=90','-quality-level=Epic','-unattended','-nosound','-notraceserver','-ddc=NoZenLocalFallback','-LocalDataCachePath=/home/william/.cache/carla-ddc','-ini:Engine:[SystemSettings]:r.RayTracing=1,[SystemSettings]:r.Nanite=1,[SystemSettings]:r.Shadow.Virtual.Enable=1,[SystemSettings]:r.Streaming.PoolSize=2048,[SystemSettings]:r.Nanite.Streaming.StreamingPoolSize=256,[SystemSettings]:r.Lumen.DiffuseIndirect.Allow=1,[SystemSettings]:r.Lumen.Reflections.Allow=1,[SystemSettings]:r.AOGlobalDFResolution=64,[SystemSettings]:r.GenerateMeshDistanceFields=1','-ini:Engine:[DevOptions.Shaders]:bAllowAsynchronousShaderCompiling=False','-ExecCmds=r.Streaming.PoolSize 2048,r.Shadow.Virtual.Enable 1,r.Nanite 1,r.Lumen.DiffuseIndirect.Allow 1,r.Lumen.Reflections.Allow 1,carla.Camera.UseRayTracing 1,py /mnt/simulations/vehicle-exteriors/lincoln-mkz/scripts/runtime_observer.py','-abslog='+str(R/'native-unreal.log')]
proc=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT);owned=[];report={'status':'RUNNING','command':command,'port':PORT,'resolution':[1280,800],'exposure_ev100':6.8777442499,'post_process_profile':'MKZExteriorStudio','controlled_paint_linear_rgb':[.024,.045,.065],'color_assignment':'Identical runtime shader color assigned to both owned actors; stock Blueprint bodywork slot does not implement color correctly.','postprocessing':'Native CARLA RGB, no DLSS or neural processing','preview_rendering':'Epic isolated studio using native Unreal scene-capture RGB, Lumen hardware ray tracing and virtual shadows. Ray tracing explicitly enabled for scene captures; exposure locked at EV100 6.88 for the studio. Existing town remains stopped by the user.'}
(R/'runtime-report.json').write_text(json.dumps(report,indent=2))
try:
    world=None;deadline=time.monotonic()+1800
    while time.monotonic()<deadline:
        if proc.poll()is not None:raise RuntimeError('Isolated engine exited: '+str(proc.returncode))
        try:
            client=carla.Client('127.0.0.1',PORT);client.set_timeout(30);world=client.get_world();settings=world.get_settings();print('RPC_READY',world.id,flush=True);break
        except RuntimeError:time.sleep(2)
    if world is None:raise TimeoutError('Isolated engine RPC readiness timeout')
    client.set_timeout(180);cfg=world.get_settings();print('SETTINGS_READ',flush=True);cfg.synchronous_mode=True;cfg.fixed_delta_seconds=.05;world.apply_settings(cfg);print('SYNC_APPLIED',flush=True);lib=world.get_blueprint_library();report['server_version']=client.get_server_version();report['vehicles']={}
    views={'front-three-quarter':((6.3,-5.4,2.7),(0,0,.8),39.4),'rear-three-quarter':((-6.3,-5.2,2.5),(0,0,.8),39.4),'front-detail':((4.4,-1.8,1.35),(1.95,0,.65),31.4),'wheel-detail':((2.55,-2.8,.9),(1.47,-.8,.371),29.6)}
    def transform(loc,target):
        dx,dy,dz=[b-a for a,b in zip(loc,target)];return carla.Transform(carla.Location(*loc),carla.Rotation(pitch=math.degrees(math.atan2(dz,math.hypot(dx,dy))),yaw=math.degrees(math.atan2(dy,dx))))
    for stage,identifier in [('stock','vehicle.lincoln.mkz'),('refined','vehicle.lincoln.mkz_refined')]:
        bp=lib.find(identifier);bp.set_attribute('color','43,60,72');v=world.spawn_actor(bp,carla.Transform(carla.Location(z=3)));owned.append(v);print('SPAWNED',identifier,v.id,flush=True);v.set_simulate_physics(False);v.set_transform(carla.Transform(carla.Location(z=.015)));v.apply_control(carla.VehicleControl(hand_brake=True))
        physics=v.get_physics_control();assert len(physics.wheels)==4;extent=v.bounding_box.extent;assert 1.5<extent.x<3 and .7<extent.y<1.2
        data={'id':identifier,'extent_m':[extent.x,extent.y,extent.z],'wheels':len(physics.wheels),'captures':{}}
        for name,(loc,target,fov)in views.items():
            cam=lib.find('sensor.camera.rgb');cam.set_attribute('image_size_x','1280');cam.set_attribute('image_size_y','800');cam.set_attribute('fov',str(fov));cam.set_attribute('enable_postprocess_effects','true');cam.set_attribute('post_process_profile','MKZExteriorStudio')
            sensor=world.spawn_actor(cam,transform(loc,target));owned.append(sensor);q=queue.Queue();sensor.listen(q.put)
            print('WARMUP',stage,name,flush=True)
            for i in range(80):
                frame=world.tick(180);im=q.get(timeout=60)
                while im.frame<frame:im=q.get(timeout=60)
                assert im.frame==frame
                if i%20==0:print('FRAME',stage,name,i,frame,flush=True)
            path=R/'renders'/('native-'+stage+'-'+name+'.png');im.save_to_disk(str(path));assert len(im.raw_data)==1280*800*4
            data['captures'][name]={'file':str(path),'frame':im.frame,'timestamp':im.timestamp,'transform':str(transform(loc,target)),'fov':fov};print('CAPTURE',stage,name,frame,flush=True)
            sensor.stop();sensor.destroy();owned.remove(sensor)
        if stage=='refined':
            # Validate the retained four-wheel physics and motion in this owned studio.
            v.set_simulate_physics(True);v.apply_control(carla.VehicleControl(hand_brake=True))
            for i in range(30):world.tick(180)
            start=v.get_location();v.apply_control(carla.VehicleControl(throttle=.35,steer=.15))
            for i in range(50):world.tick(180)
            end=v.get_location();distance=start.distance(end);assert distance>.15,('Vehicle did not move',distance)
            data['movement_distance_m']=distance;data['final_location_m']=[end.x,end.y,end.z]
        report['vehicles'][stage]=data;v.destroy();owned.remove(v)
    report['status']='PASS_NATIVE_CAPTURE_AND_PHYSICS';(R/'runtime-report.json').write_text(json.dumps(report,indent=2))
except Exception as e:
    report.update(status='FAILED',error=str(e));(R/'runtime-report.json').write_text(json.dumps(report,indent=2));raise
finally:
    for a in reversed(owned):
        try:
            if a.type_id.startswith('sensor.'):a.stop()
            a.destroy()
        except RuntimeError:pass
    if proc.poll()is None:
        try:
            if world is not None:
                shutdown_settings=world.get_settings();shutdown_settings.synchronous_mode=False;world.apply_settings(shutdown_settings)
        except Exception:pass
        stop.touch()
        try:proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:proc.wait(timeout=15)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    stop.unlink(missing_ok=True);log.close();report['engine_returncode']=proc.returncode;(R/'runtime-report.json').write_text(json.dumps(report,indent=2))
