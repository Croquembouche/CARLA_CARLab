import bpy,json
from mathutils import Vector
from pathlib import Path
from collections import defaultdict
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));report={}
for o in bpy.context.scene.objects:
 if o.type!='MESH':continue
 if o.name=='SK_LincolnMKZ.001':
  me=o.data;parents=list(range(len(me.vertices)))
  def root(i):
   while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
   return i
  for e in me.edges:
   a,b=map(root,e.vertices);parents[a]=b
  groups=defaultdict(list)
  for v in me.vertices:groups[root(v.index)].append(v.index)
  result=[]
  for ids in groups.values():
   if len(ids)<6:continue
   pts=[o.matrix_world@me.vertices[i].co for i in ids];lo=[min(v[k] for v in pts) for k in range(3)];hi=[max(v[k] for v in pts) for k in range(3)]
   if lo[0]>.2 and lo[2]>.6 and hi[2]<1.25:result.append({'n':len(ids),'lo':lo,'hi':hi,'first':ids[0]})
  report['stock_parts']=result
 if o.name.startswith(('Brochure_','Front_Right')):
  pts=[o.matrix_world@Vector(p) for p in o.bound_box];report[o.name]={'lo':[min(v[k] for v in pts) for k in range(3)],'hi':[max(v[k] for v in pts) for k in range(3)],'materials':[m.name for m in o.data.materials]}
(R/'brochure-geometry-inspect.json').write_text(json.dumps(report,indent=2))
