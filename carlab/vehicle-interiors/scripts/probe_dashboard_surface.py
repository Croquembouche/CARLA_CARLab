import bpy,json
from mathutils import Vector
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));dg=bpy.context.evaluated_depsgraph_get();obs=[o.evaluated_get(dg) for o in bpy.context.scene.objects if o.type=='MESH' and (o.name=='SK_LincolnMKZ.001' or o.name.startswith('Brochure_Dashboard'))];out=[]
for y in [-.74,-.70,-.66,-.62,-.58,-.1,-.05,0,.05,.1]:
 for z in [.86,.88,.90,.92,.94,.96,.98,1.0]:
  hits=[]
  for o in obs:
   inv=o.matrix_world.inverted();hit,p,n,f=o.ray_cast(inv@Vector((.65,y,z)),(inv.to_3x3()@Vector((1,0,0))).normalized())
   if hit:hits.append((float((o.matrix_world@p).x),o.name))
  out.append([y,z,min(hits) if hits else None])
(R/'dashboard-surface-probe.json').write_text(json.dumps(out))
