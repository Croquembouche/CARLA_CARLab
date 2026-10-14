"""Brochure material calibration; geometry and camera lighting remain unchanged."""
import bpy,shutil,json
from pathlib import Path
R=ROOT=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));backup=R/'backups/before-materials-v3';backup.mkdir(exist_ok=True)
if not (backup/'Lincoln_MKZ_Interior_Brochure.blend').exists():shutil.copy2(R/'Lincoln_MKZ_Interior_Brochure.blend',backup/'Lincoln_MKZ_Interior_Brochure.blend')
for name in ['T_MKZ_Brochure_BaseColor.png','T_MKZ_Brochure_ORM.png','T_MKZ_Leather_Roughness.png','T_MKZ_Perforated_Roughness.png']:
 if not (backup/name).exists():shutil.copy2(R/'exports'/name,backup/name)
# Leather retains the existing physical grain/perforation normals; correct tone and luster.
for name,c in [('MKZ_Ebony_Leather',(.007,.008,.009)),('MKZ_Perforated_Leather',(.007,.008,.009))]:
 p=bpy.data.materials[name].node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Specular IOR Level'].default_value=.4
p=bpy.data.materials['MKZ_Stitch_Charcoal'].node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.022,.024,.026,1)
for name in ['MKZ_Fine_Satin','MKZ_Brochure_Satin']:
 if name not in bpy.data.materials:continue
 p=bpy.data.materials[name].node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.20,.21,.22,1);p.inputs['Metallic'].default_value=1;p.inputs['Roughness'].default_value=.30;p.inputs['Anisotropic'].default_value=.65
m=bpy.data.materials['MI_LincolnMKZ_Interior01'];nt=m.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
# Update only the known procedural veneer ramp (not the photographed control graphics).
ramps=[]
for n in nt.nodes:
 if n.type=='VALTORGB' and abs(n.color_ramp.elements[0].color[0]-.04)<.0001 and abs(n.color_ramp.elements[1].color[0]-.18)<.0001:ramps.append(n)
assert len(ramps)==1,len(ramps)
n=ramps[0];n.name='V3 Subdued Walnut';n.color_ramp.elements[0].color=(.020,.010,.006,1);n.color_ramp.elements[1].color=(.090,.045,.024,1)
orig=next(n for n in nt.nodes if n.type=='SEPARATE_COLOR' and n.inputs[0].links and n.inputs[0].links[0].from_node.type=='TEX_IMAGE' and 'orm' in n.inputs[0].links[0].from_node.image.name)
assert not nt.nodes.get('V3 metal response'),'Use the before-materials-v3 source before rebuilding this calibration'
mask=nt.nodes.new('ShaderNodeMath');mask.operation='GREATER_THAN';mask.inputs[1].default_value=.65;nt.links.new(orig.outputs['Blue'],mask.inputs[0]);mask.name='V3 metal response'
old=p.inputs['Base Color'].links[0].from_socket;mul=nt.nodes.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1;mul.inputs[2].default_value=(.52,.55,.58,1);nt.links.new(old,mul.inputs[1]);mix=nt.nodes.new('ShaderNodeMixRGB');nt.links.new(mask.outputs[0],mix.inputs[0]);nt.links.new(old,mix.inputs[1]);nt.links.new(mul.outputs[0],mix.inputs[2]);nt.links.new(mix.outputs[0],p.inputs['Base Color'])
old=p.inputs['Roughness'].links[0].from_socket;mix=nt.nodes.new('ShaderNodeMixRGB');nt.links.new(mask.outputs[0],mix.inputs[0]);nt.links.new(old,mix.inputs[1]);mix.inputs[2].default_value=(.29,.29,.29,1);nt.links.new(mix.outputs[0],p.inputs['Roughness'])
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
source=(R/'scripts/export_finish.py').read_text();source=source[source.index("source=bpy.data.materials['MI_LincolnMKZ_Interior01']"):].replace('T_MKZ_Ebony_','T_MKZ_Brochure_');exec(compile(source,'v3-atlas-bake','exec'))
