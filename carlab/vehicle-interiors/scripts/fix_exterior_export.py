import bpy,bmesh,json
from pathlib import Path
from mathutils import Matrix,Vector
R=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(R/'reference/lincoln-stock.blend'))
s=bpy.context.scene;body=bpy.data.objects['SK_LincolnMKZ.001'];old=bpy.data.objects['Vehicle_Base']
print('ORIGINAL_ARM',old.matrix_world,flush=True)
print('ORIGINAL_BONE',old.matrix_world@old.data.bones[0].matrix_local,flush=True)
world=body.matrix_world.copy();heads={b.name:(old.matrix_world@b.matrix_local).translation.copy()*100 for b in old.data.bones}
for mod in list(body.modifiers):body.modifiers.remove(mod)
body.parent=None;body.matrix_world=world;bpy.ops.object.select_all(action='DESELECT');body.select_set(True);bpy.context.view_layer.objects.active=body;bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
bm=bmesh.new();bm.from_mesh(body.data);bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index==2],context='FACES');bmesh.ops.scale(bm,vec=Vector((100,100,100)),verts=list(bm.verts));bm.to_mesh(body.data);bm.free()
s.unit_settings.system='METRIC';s.unit_settings.scale_length=.01
rig=bpy.data.armatures.new('ExteriorCentimeterRig');arm=bpy.data.objects.new('Armature',rig);s.collection.objects.link(arm)
bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);bpy.context.view_layer.objects.active=arm;bpy.ops.object.mode_set(mode='EDIT')
root=rig.edit_bones.new('Vehicle_Base');root.head=(0,0,0);root.tail=(0,15,0)
for name,head in heads.items():
 b=rig.edit_bones.new(name);b.head=head;b.tail=head+Vector((0,12,0));b.parent=root
bpy.ops.object.mode_set(mode='OBJECT');body.parent=arm;mod=body.modifiers.new('Vehicle rig','ARMATURE');mod.object=arm
vg=body.vertex_groups.get('Vehicle_Base') or body.vertex_groups.new(name='Vehicle_Base');vg.add([v.index for v in body.data.vertices if not any(g.weight>0 for g in v.groups)],1,'REPLACE')
body.name='SK_MKZ_ExteriorOnly';bpy.ops.object.select_all(action='DESELECT');body.select_set(True);arm.select_set(True)
bpy.ops.export_scene.fbx(filepath=str(R/'exports/SK_MKZ_ExteriorOnly.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,axis_forward='-Y',axis_up='Z')
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Rig.blend'))
