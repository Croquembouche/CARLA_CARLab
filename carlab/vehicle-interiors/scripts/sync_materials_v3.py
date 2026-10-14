import bpy
from pathlib import Path
R=ROOT=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'))
for im in bpy.data.images:
 if any(k in im.name for k in ['T_MKZ_Leather_Roughness','T_MKZ_Perforated_Roughness','T_MKZ_Perforated_Normal']):
  im.filepath=str(R/'exports'/im.name);im.reload()
def tex(nt,name,tiling=1):
 im=bpy.data.images.load(str(R/'exports'/(name+'.png')),check_existing=True);im.colorspace_settings.name='Non-Color';im.reload();n=nt.nodes.new('ShaderNodeTexImage');n.image=im
 if tiling!=1:
  uv=nt.nodes.new('ShaderNodeTexCoord');v=nt.nodes.new('ShaderNodeVectorMath');v.operation='MULTIPLY';v.inputs[1].default_value=(tiling,tiling,tiling);nt.links.new(uv.outputs['UV'],v.inputs[0]);nt.links.new(v.outputs[0],n.inputs['Vector'])
 return n
m=bpy.data.materials['MKZ_Perforated_Leather'];nt=m.node_tree;p=nt.nodes.get('Principled BSDF');n=tex(nt,'T_MKZ_Perforation_Mask');mix=nt.nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;mix.inputs[1].default_value=(.007,.008,.009,1);nt.links.new(n.outputs['Color'],mix.inputs[2]);nt.links.new(mix.outputs[0],p.inputs['Base Color'])
m=bpy.data.materials['MKZ_Fine_Satin'];nt=m.node_tree;p=nt.nodes.get('Principled BSDF');n=tex(nt,'T_MKZ_Brushed_Roughness',8);nt.links.new(n.outputs[0],p.inputs['Roughness']);n=tex(nt,'T_MKZ_Brushed_Normal',8);nm=nt.nodes.new('ShaderNodeNormalMap');nt.links.new(n.outputs[0],nm.inputs['Color']);nt.links.new(nm.outputs[0],p.inputs['Normal'])
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
source=(R/'scripts/export_finish.py').read_text();source=source[source.index("source=bpy.data.materials['MI_LincolnMKZ_Interior01']"):].replace('T_MKZ_Ebony_','T_MKZ_Brochure_');exec(compile(source,'v3-atlas-bake','exec'))
