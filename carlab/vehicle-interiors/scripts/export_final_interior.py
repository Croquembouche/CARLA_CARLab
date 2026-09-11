import bpy,bmesh,json
from pathlib import Path
from mathutils import Matrix
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
s=bpy.context.scene;car=bpy.data.objects['SK_LincolnMKZ.001'];cabin=car.copy();cabin.data=car.data.copy();s.collection.objects.link(cabin)
bm=bmesh.new();bm.from_mesh(cabin.data);bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index!=2],context='FACES');bm.to_mesh(cabin.data);bm.free()
bpy.ops.object.select_all(action='DESELECT');cabin.select_set(True)
for o in list(s.objects):
 if o.name.startswith(('Front_Left_','Front_Right_','Rear_Left_','Rear_Right_','Rear_Center_')) or o.name=='SM_LincolnCabinDetails':o.select_set(True)
bpy.context.view_layer.objects.active=cabin;bpy.ops.object.convert(target='MESH');bpy.ops.object.join();cabin=bpy.context.object;cabin.name='SM_MKZ_Cabin_Final'
s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.export_scene.fbx(filepath=str(R/'exports/SM_MKZ_Cabin_Final.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')
r={'cabin_faces':len(cabin.data.polygons),'cabin_vertices':len(cabin.data.vertices),'materials':[m.name for m in cabin.data.materials]}
# Export the exterior with the stock centimeter reference pose.
exec(compile((R/'scripts/fix_exterior_export.py').read_text(),str(R/'scripts/fix_exterior_export.py'),'exec'))
