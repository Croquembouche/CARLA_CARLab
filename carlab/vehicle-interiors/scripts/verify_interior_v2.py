import bpy,json,math
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
scene=bpy.context.scene
for name in ['MKZ_Dashboard','MKZ_Driver_View','MKZ_Rear_Seats','MKZ_Front_Seats']:
 assert bpy.data.objects.get(name) and bpy.data.objects[name].type=='CAMERA',name
for row in ['Front','Rear']:
 for side in ['Left','Right']:
  for part in ['Backrest','Cushion','Headrest','Seatbelt_Buckle']:
   assert bpy.data.objects.get(f'{row}_{side}_{part}'),(row,side,part)
for obj in scene.objects:
 if obj.type=='MESH':
  assert all(math.isfinite(x) for v in obj.data.vertices for x in v.co),obj.name
assert bpy.data.objects['SK_SeatedMannequin'].hide_render
assert all(im.packed_file for im in bpy.data.images if im.source=='FILE' and im.size[0]>0)
report={'result':'PASS','cameras':4,'outboard_seats':4,'rear_center_seat':bool(bpy.data.objects.get('Rear_Center_Cushion')),'stitch_curves':sum(o.type=='CURVE' and 'Stitch' in o.name for o in scene.objects),'textures_packed':True,'finite_geometry':True}
(R/'interior-v2-verification.json').write_text(json.dumps(report,indent=2));print(report)
