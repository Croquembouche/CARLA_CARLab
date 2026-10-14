"""Protect the metal control-panel photo from the ebony upholstery color mask."""
import bpy,json
from pathlib import Path
ROOT=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_Brochure.blend'))
m=bpy.data.materials['MI_LincolnMKZ_Interior01'];nt=m.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED');d=next(n for n in nt.nodes if n.type=='TEX_IMAGE' and n.image.name.endswith('_d.tga'));orig=next(n for n in nt.nodes if n.type=='SEPARATE_COLOR' and n.inputs[0].links and n.inputs[0].links[0].from_node.type=='TEX_IMAGE' and 'orm' in n.inputs[0].links[0].from_node.image.name)
assert not nt.nodes.get('Brochure control panel protection'),'Already applied'
uv=nt.nodes.new('ShaderNodeTexCoord');xy=nt.nodes.new('ShaderNodeSeparateXYZ');nt.links.new(uv.outputs['UV'],xy.inputs[0]);rgb=nt.nodes.new('ShaderNodeSeparateColor');nt.links.new(d.outputs['Color'],rgb.inputs[0])
def mathn(op,a,b):
 n=nt.nodes.new('ShaderNodeMath');n.operation=op
 for i,v in enumerate([a,b]):
  if isinstance(v,(int,float)):n.inputs[i].default_value=v
  else:nt.links.new(v,n.inputs[i])
 return n.outputs[0]
mask=mathn('MULTIPLY',mathn('GREATER_THAN',xy.outputs['X'],1680/4096),mathn('LESS_THAN',xy.outputs['X'],2380/4096));mask=mathn('MULTIPLY',mask,mathn('GREATER_THAN',xy.outputs['Y'],1-2020/4096));mask=mathn('MULTIPLY',mask,mathn('LESS_THAN',xy.outputs['Y'],1-1680/4096));mask=mathn('MULTIPLY',mask,mathn('LESS_THAN',rgb.outputs['Red'],.22));mask.node.name='Brochure control panel protection'
for pin,new in [('Base Color',d.outputs['Color']),('Roughness',orig.outputs['Green'])]:
 old=p.inputs[pin].links[0].from_socket;n=nt.nodes.new('ShaderNodeMixRGB');nt.links.new(mask,n.inputs[0]);nt.links.new(old,n.inputs[1]);nt.links.new(new,n.inputs[2]);nt.links.new(n.outputs[0],p.inputs[pin])
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/f'Lincoln_MKZ_Interior_{name}.blend'))
source=(ROOT/'scripts/export_finish.py').read_text();source=source[source.index("source=bpy.data.materials['MI_LincolnMKZ_Interior01']"):].replace('T_MKZ_Ebony_','T_MKZ_Brochure_');exec(compile(source,'brochure-atlas-bake','exec'))
(ROOT/'brochure-console-shader-fix.json').write_text(json.dumps({'result':'BAKED','cause':'Ebony color classification accidentally altered control-panel photo pixels','fix':'UV and color constrained protection of original control-panel colors and roughness','textures':['T_MKZ_Brochure_BaseColor','T_MKZ_Brochure_ORM']},indent=2))
