import bpy
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'))
for name,color in [('MKZ_Ebony_Leather',(.013,.015,.017)),('MKZ_Perforated_Leather',(.012,.014,.016))]:
 m=bpy.data.materials[name];p=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
 for link in list(p.inputs['Base Color'].links):m.node_tree.links.remove(link)
 p.inputs['Base Color'].default_value=(*color,1);p.inputs['Specular IOR Level'].default_value=.35;m.diffuse_color=(*color,1)
for image in bpy.data.images:
 if image.name.startswith(('T_MKZ_Leather_Roughness','T_MKZ_Perforated_Roughness')):
  image.filepath=str(R/'exports'/image.name);image.reload();image.pack()
p=next(n for n in bpy.data.materials['MKZ_Brochure_Satin'].node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Roughness'].default_value=.4
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
exec(compile((R/'scripts/export_completed_cabin.py').read_text(),'export_completed_cabin.py','exec'))
