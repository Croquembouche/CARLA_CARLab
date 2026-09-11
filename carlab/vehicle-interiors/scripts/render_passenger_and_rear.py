import bpy,json,shutil,time
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
s=bpy.context.scene
old_camera=s.camera
name='MKZ_Passenger_Seat'
cam=bpy.data.objects.get(name)
if cam is None:
 data=bpy.data.cameras.new(name);cam=bpy.data.objects.new(name,data);s.collection.objects.link(cam)
cam.location=(.65,.35,1.28);cam.rotation_euler=(Vector((-.08,-.40,.96))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=24;cam.data.clip_start=.012
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
devices=[d for d in prefs.devices if d.type=='OPTIX']
assert len(devices)>2,'Expected free GPU 2 for this render'
for device in prefs.devices:device.use=device==devices[2]
s.render.engine='CYCLES';s.cycles.device='GPU';s.cycles.samples=128;s.cycles.use_denoising=True
s.render.resolution_x=1600;s.render.resolution_y=1200;s.render.resolution_percentage=100
s.render.image_settings.file_format='PNG'
report=[]
for camera_name,filename in [('MKZ_Passenger_Seat','Lincoln_Passenger_Seat.png'),('MKZ_Rear_Seats','Lincoln_Back_Seat.png')]:
 s.camera=bpy.data.objects[camera_name];s.render.filepath=str(R/'renders'/filename)
 start=time.monotonic();bpy.ops.render.render(write_still=True)
 report.append({'camera':camera_name,'file':s.render.filepath,'seconds':round(time.monotonic()-start,1)})
s.camera=old_camera
backup=R/'backups'/'before-passenger-render.blend'
if not backup.exists():shutil.copy2(R/'Lincoln_MKZ_Interior_v2.blend',backup)
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
(R/'passenger-rear-render-report.json').write_text(json.dumps(report,indent=2))
