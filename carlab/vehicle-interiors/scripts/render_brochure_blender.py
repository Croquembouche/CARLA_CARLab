import bpy
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));s=bpy.context.scene
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices();ds=[d for d in prefs.devices if d.type=='OPTIX']
for d in prefs.devices:d.use=d==ds[3]
s.cycles.device='GPU';s.cycles.samples=64;s.cycles.use_denoising=True;s.render.resolution_x=1280;s.render.resolution_y=800;s.render.resolution_percentage=100
bpy.data.objects['SK_SeatedMannequin'].hide_render=True
for o in s.objects:
 if '_Worn_' in o.name:o.hide_render=True
bpy.ops.object.camera_add(location=(.38,-.67,1.17));c=bpy.context.object;c.rotation_euler=(Vector((-.45,.12,.93))-c.location).to_track_quat('-Z','Y').to_euler();c.data.lens=18;c.data.clip_start=.008;s.camera=c;s.render.filepath=str(R/'renders/Brochure_Seat_Study.png');bpy.ops.render.render(write_still=True)
