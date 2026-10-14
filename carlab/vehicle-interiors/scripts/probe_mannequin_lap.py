import bpy,json
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));o=bpy.data.objects['SK_SeatedMannequin'].evaluated_get(bpy.context.evaluated_depsgraph_get());iv=o.matrix_world.inverted();r=[]
for z in [.6,.65,.7,.75,.8]:
 row=[]
 for y in [.16,.22,.3,.4,.5,.57,.62]:
  hit,co,no,i=o.ray_cast(iv@Vector((1.1,y,z)),(iv.to_3x3()@Vector((-1,0,0))).normalized());row.append(round((o.matrix_world@co).x,3) if hit else None)
 r.append({'z':z,'front_x_y16_22_30_40_50_57_62':row})
(R/'mannequin-lap-probe.json').write_text(json.dumps(r,indent=2))
