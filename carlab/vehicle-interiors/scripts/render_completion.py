import bpy,math
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));s=bpy.context.scene
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices();ds=[d for d in prefs.devices if d.type=='OPTIX']
for d in prefs.devices:d.use=d==ds[3]
s.cycles.device='GPU';s.cycles.samples=48;s.cycles.use_denoising=True;s.render.resolution_x=1200;s.render.resolution_y=900;s.render.resolution_percentage=100
bpy.data.objects['SK_SeatedMannequin'].hide_render=False
for name,loc,target in [('Completed_Roof',(-.4,0,.79),(-.2,0,1.53)),('Completed_Belts',(.70,-.32,1.25),(.15,.40,.98)),('Completed_Rear',(-.43,.05,1.22),(-1.05,0,.87))]:
 bpy.ops.object.camera_add(location=loc);c=bpy.context.object;c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler();c.data.lens=22;s.camera=c;s.render.filepath=str(R/'renders'/(name+'.png'));bpy.ops.render.render(write_still=True)
