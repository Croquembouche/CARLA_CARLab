import bpy,json
from mathutils import Vector
from collections import defaultdict
bpy.ops.wm.open_mainfile(filepath='/mnt/simulations/vehicle-interiors/Lincoln_Interior_Mannequin.blend')
result={}
for o in bpy.context.scene.objects:
 if o.type!='MESH' or not o.name.startswith(('SK_Lincoln','SM_LincolnMKZ_Door')):continue
 m=o.data;parents=list(range(len(m.vertices)))
 def find(x):
  while parents[x]!=x:parents[x]=parents[parents[x]];x=parents[x]
  return x
 for e in m.edges:
  a,b=map(find,e.vertices);parents[a]=b
 groups=defaultdict(list)
 for v in m.vertices:groups[find(v.index)].append(v.index)
 rows=[]
 for k,vs in groups.items():
  if len(vs)<40:continue
  co=[o.matrix_world@m.vertices[i].co for i in vs]
  lo=[round(min(c[j] for c in co),3) for j in range(3)];hi=[round(max(c[j] for c in co),3) for j in range(3)]
  rows.append({'id':k,'n':len(vs),'lo':lo,'hi':hi})
 result[o.name]={'uv':[u.name for u in m.uv_layers],'parts':sorted(rows,key=lambda r:-r['n'])}
open('/mnt/simulations/vehicle-interiors/reference/parts.json','w').write(json.dumps(result,indent=2))
