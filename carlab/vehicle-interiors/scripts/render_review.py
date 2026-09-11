import bpy
from mathutils import Vector
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
s=bpy.context.scene
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
optix=[d for d in prefs.devices if d.type=='OPTIX']
for d in prefs.devices:d.use=bool(optix) and d==optix[min(1,len(optix)-1)]
s.cycles.device='GPU'
cam=bpy.data.objects['MKZ_Rear_Seats'];cam.data.lens=15;cam.rotation_euler=(Vector((-1.20,0,.92))-cam.location).to_track_quat('-Z','Y').to_euler()

for old in list(bpy.data.objects):
 if old.name.startswith('MKZ_Front_Seats'):bpy.data.objects.remove(old,do_unlink=True)
bpy.ops.object.camera_add(location=(.60,-.60,1.25));front=bpy.context.object;front.name='MKZ_Front_Seats';front.data.lens=19;front.data.clip_start=.012;front.rotation_euler=(Vector((-.15,.04,.92))-front.location).to_track_quat('-Z','Y').to_euler()
s.camera=front;bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
for camera in [front,cam]:
 s.camera=camera;s.render.filepath=str(R/'renders'/(camera.name+'.png'));bpy.ops.render.render(write_still=True)
