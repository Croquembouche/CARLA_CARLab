import bpy,json
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));o=bpy.data.objects['SK_LincolnMKZ.001'];inv=o.matrix_world.inverted();r=[]
for x in [-.6,-.5,-.4,-.3,-.2,-.1,0,.1,.2,.3,.4,.5,.6,.7,.8,.9,1.0]:
 row={'x':x,'hits':[]}
 for y in [0,.06,.12,.17]:
  origin=Vector((x,y,1.08));hit,loc,normal,idx=o.ray_cast(inv@origin,(inv.to_3x3()@Vector((0,0,-1))).normalized())
  row['hits'].append({'y':y,'z':round((o.matrix_world@loc).z,4) if hit else None,'material':o.data.polygons[idx].material_index if hit else None})
 r.append(row)
(R/'center-console-measure.json').write_text(json.dumps(r,indent=2))
