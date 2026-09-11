"""Bake reference-led shader onto the original UV atlas for matching UE appearance."""
import bpy
from pathlib import Path
ROOT=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_v2.blend'))
# Copy only the stock cabin material section, then join the replacement seats and detail.
# The UE variant masks this same material section on its original skeletal mesh.
import bmesh
car=bpy.data.objects['SK_LincolnMKZ.001'];cabin=car.copy();cabin.data=car.data.copy();bpy.context.scene.collection.objects.link(cabin)
bm=bmesh.new();bm.from_mesh(cabin.data);bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index!=2],context='FACES');bm.to_mesh(cabin.data);bm.free()
bpy.ops.object.select_all(action='DESELECT');cabin.select_set(True)
for o in list(bpy.context.scene.objects):
 if o.name.startswith(('Front_Left_','Front_Right_','Rear_Left_','Rear_Right_','Rear_Center_')) or o.name=='SM_LincolnCabinDetails':o.select_set(True)
bpy.context.view_layer.objects.active=cabin
bpy.ops.object.convert(target='MESH');bpy.ops.object.join();cabin=bpy.context.object;cabin.name='SM_MKZ_Cabin_v2'
bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.export_scene.fbx(filepath=str(ROOT/'exports/SM_MKZ_Cabin_v2.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')
source=bpy.data.materials['MI_LincolnMKZ_Interior01']
scene=bpy.data.scenes.new('AtlasBake');bpy.context.window.scene=scene
scene.render.engine='CYCLES';scene.cycles.samples=8;scene.cycles.use_denoising=False
scene.render.resolution_x=4096;scene.render.resolution_y=4096;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
bpy.ops.mesh.primitive_plane_add(size=2);plane=bpy.context.object
bpy.ops.object.camera_add(location=(0,0,3));cam=bpy.context.object;cam.data.type='ORTHO';cam.data.ortho_scale=2;scene.camera=cam
for suffix in ['BaseColor','ORM']:
 m=source.copy();nt=m.node_tree;bsdf=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED');out=next(n for n in nt.nodes if n.type=='OUTPUT_MATERIAL')
 em=nt.nodes.new('ShaderNodeEmission');nt.links.new(em.outputs[0],out.inputs['Surface'])
 if suffix=='BaseColor':
  nt.links.new(bsdf.inputs['Base Color'].links[0].from_socket,em.inputs['Color']);scene.view_settings.view_transform='Standard'
 else:
  orig=next(n for n in nt.nodes if n.type=='SEPARATE_COLOR' and n.inputs[0].links and n.inputs[0].links[0].from_node.type=='TEX_IMAGE' and 'orm' in n.inputs[0].links[0].from_node.image.name)
  combine=nt.nodes.new('ShaderNodeCombineColor');nt.links.new(orig.outputs['Red'],combine.inputs['Red']);nt.links.new(bsdf.inputs['Roughness'].links[0].from_socket,combine.inputs['Green']);nt.links.new(orig.outputs['Blue'],combine.inputs['Blue']);nt.links.new(combine.outputs[0],em.inputs['Color']);scene.view_settings.view_transform='Raw'
 plane.data.materials.clear();plane.data.materials.append(m)
 scene.render.filepath=str(ROOT/'exports'/('T_MKZ_Ebony_'+suffix+'.png'));bpy.ops.render.render(write_still=True)
print('MKZ_FINISH_EXPORT_COMPLETE')
