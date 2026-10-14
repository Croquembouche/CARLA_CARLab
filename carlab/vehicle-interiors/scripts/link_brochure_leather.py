import bpy
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'))
for kind,name in [('Leather','MKZ_Ebony_Leather'),('Perforated','MKZ_Perforated_Leather')]:
 m=bpy.data.materials[name];nt=m.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
 for suffix in ['Normal','Roughness']:
  im=bpy.data.images.load(str(R/'exports'/f'T_MKZ_{kind}_{suffix}.png'),check_existing=True);im.colorspace_settings.name='Non-Color';tex=nt.nodes.new('ShaderNodeTexImage');tex.image=im
  if suffix=='Normal':
   normal=nt.nodes.new('ShaderNodeNormalMap');nt.links.new(tex.outputs[0],normal.inputs['Color']);nt.links.new(normal.outputs[0],p.inputs['Normal'])
  else:nt.links.new(tex.outputs[0],p.inputs['Roughness'])
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
