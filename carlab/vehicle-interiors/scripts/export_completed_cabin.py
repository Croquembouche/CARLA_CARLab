import bpy,bmesh
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));s=bpy.context.scene;col=bpy.data.collections['Interior_Completion']
def export(objects,name):
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 bpy.ops.export_scene.fbx(filepath=str(R/'exports'/(name+'.fbx')),use_selection=True,object_types={'MESH','OTHER'},bake_anim=False,axis_forward='-Y',axis_up='Z')
car=bpy.data.objects['SK_LincolnMKZ.001'];cabin=car.copy();cabin.data=car.data.copy();s.collection.objects.link(cabin)
bm=bmesh.new();bm.from_mesh(cabin.data);bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index!=2],context='FACES');bm.to_mesh(cabin.data);bm.free()
objects=[cabin]+[o for o in s.objects if (o.name.startswith(('Front_Left_','Front_Right_','Rear_Left_','Rear_Right_','Rear_Center_')) or o.name.startswith('Brochure_') or o.name=='SM_LincolnCabinDetails') and o.name not in col.objects]
bpy.ops.object.select_all(action='DESELECT')
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=cabin;bpy.ops.object.convert(target='MESH');bpy.ops.object.join();cabin=bpy.context.object;s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');export([cabin],'SM_MKZ_Cabin_Final')

export([cabin],'SM_MKZ_Cabin_Brochure')
