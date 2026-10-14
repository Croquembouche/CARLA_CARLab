import bpy
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));s=bpy.context.scene
for o in s.objects:o.hide_render=o.name!='SK_LincolnMKZ.001' and not o.name.startswith('Brochure_Dashboard')
for name in ['MI_LincolnMKZ_Interior01','MI_LincolnMKZ_Interior2']:
 m=bpy.data.materials.get(name)
 if not m:continue
 nt=m.node_tree;p=nt.nodes.get('Principled BSDF');out=next(n for n in nt.nodes if n.type=='OUTPUT_MATERIAL');em=nt.nodes.new('ShaderNodeEmission')
 if p and p.inputs['Base Color'].is_linked:nt.links.new(p.inputs['Base Color'].links[0].from_socket,em.inputs['Color'])
 elif p:em.inputs['Color'].default_value=p.inputs['Base Color'].default_value
 nt.links.new(em.outputs[0],out.inputs[0])
s.render.engine='CYCLES';s.cycles.device='CPU';s.cycles.samples=4;s.render.resolution_x=1800;s.render.resolution_y=600;s.render.resolution_percentage=100;s.view_settings.view_transform='Standard'
bpy.ops.object.camera_add(location=(.65,0,.9));c=bpy.context.object;c.rotation_euler=Vector((1,0,0)).to_track_quat('-Z','Y').to_euler();c.data.type='ORTHO';c.data.ortho_scale=1.8;c.data.clip_start=.001;s.camera=c;s.render.filepath=str(R/'renders/Dashboard_Measure.png');bpy.ops.render.render(write_still=True)
