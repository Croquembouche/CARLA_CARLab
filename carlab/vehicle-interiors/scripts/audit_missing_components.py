import bpy,json
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
r=[]
for o in bpy.context.scene.objects:
 if o.type not in {'MESH','ARMATURE'} or any(k in o.name for k in ['Stitch','Seam','Perforat']):continue
 bb=[o.matrix_world@Vector(x) for x in o.bound_box]
 r.append({'name':o.name,'hidden':o.hide_render,'bounds':[[min(v[i] for v in bb),max(v[i] for v in bb)] for i in range(3)],'materials':[m.name if m else None for m in o.data.materials] if o.type=='MESH' else []})
(R/'missing-components-audit.json').write_text(json.dumps(r,indent=2))
