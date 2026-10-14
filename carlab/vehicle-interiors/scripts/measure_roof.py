import bpy,json
from mathutils import Vector
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'));o=bpy.data.objects['SK_LincolnMKZ.001'];inv=o.matrix_world.inverted();out=[]
for x in [-1.5,-1.3,-1.1,-.9,-.7,-.5,-.3,-.1,.1,.3,.5,.7]:
 row=[]
 for y in [0,.3,.5,.6,.65,.7]:
  ok,pt,normal,face=o.ray_cast(inv@Vector((x,y,3)),(inv.to_3x3()@Vector((0,0,-1))).normalized())
  row.append(round((o.matrix_world@pt).z,3) if ok else None)
 out.append([x,row])
print(json.dumps(out),flush=True)
# Broad roof view to inspect the opening and structural trim.
bpy.ops.object.camera_add(location=(.1,-.0,4));cam=bpy.context.object;cam.rotation_euler=(Vector((-.4,0,1))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=3.1
s=bpy.context.scene;s.camera=cam;s.render.resolution_x=1100;s.render.resolution_y=850;s.render.resolution_percentage=100;s.cycles.samples=32
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='OPTIX' and '4347e9e3' in d.id
if not any(d.use for d in prefs.devices):
 ds=[d for d in prefs.devices if d.type=='OPTIX'];ds[3].use=True
s.cycles.device='GPU';s.render.filepath=str(R/'renders/Audit_Roof_Before.png');bpy.ops.render.render(write_still=True)
