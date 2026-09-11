from pathlib import Path
import subprocess,time,json,carla,socket
R=Path('/mnt/simulations/vehicle-interiors')
with socket.socket() as s:s.bind(('127.0.0.1',2230))
f=(R/'spawn-diagnostic.log').open('w');p=subprocess.Popen(['/mnt/simulations/bin/carla-editor','/Game/Carla/Maps/Town10HD_Opt','-game','-RenderOffScreen','-graphicsadapter=1','-quality-level=Epic','-ini:Engine:[DevOptions.Shaders]:bAllowCompilingThroughWorkers=False','-ini:Engine:[DevOptions.Shaders]:bAllowAsynchronousShaderCompiling=False','-ExecCmds=r.Streaming.PoolSize 2048,r.Shadow.Virtual.MaxPhysicalPages 2048','-carla-rpc-port=2230','-unattended','-nosound','-ddc=LincolnInteriorDDC'],stdout=f,stderr=subprocess.STDOUT)
a=[];r={}
try:
 c=carla.Client('127.0.0.1',2230);c.set_timeout(5)
 w=None
 deadline=time.monotonic()+600
 last_error=None
 while time.monotonic()<deadline:
  if p.poll() is not None:raise RuntimeError('Engine exited')
  try:
   c=carla.Client('127.0.0.1',2230);c.set_timeout(5);w=c.get_world();break
  except RuntimeError as e:last_error=str(e);time.sleep(2)
 if w is None:raise TimeoutError('World did not become ready: '+str(last_error))
 c.set_timeout(60);lib=w.get_blueprint_library();points=w.get_map().get_spawn_points()
 ids=[b.id for b in lib.filter('vehicle.lincoln.*')];r['ids']=ids;print(ids,flush=True)
 for identifier in [i for i in ids if i!='vehicle.lincoln.mkz_interior'][:1]+['vehicle.lincoln.mkz_interior']:
  try:
   tr=points[0];tr.location.z+=.2;v=w.spawn_actor(lib.find(identifier),tr);a.append(v)
   e=v.bounding_box.extent;r[identifier]={'actor':v.id,'wheels':len(v.get_physics_control().wheels),'extent':[e.x,e.y,e.z]};v.destroy();a.remove(v)
  except Exception as error:r[identifier]={'error':str(error)}
  print(identifier,r[identifier],flush=True)
finally:
 for v in a:
  try:v.destroy()
  except Exception:pass
 if p.poll() is None:
  p.terminate()
  try:p.wait(timeout=15)
  except subprocess.TimeoutExpired:p.kill();p.wait()
 f.close();(R/'spawn-diagnostic.json').write_text(json.dumps(r,indent=2))
