import bpy,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'))
r={}
for o in bpy.context.scene.objects:
 if o.type=='MESH' and ('Headrest' in o.name or o.name=='Front_Right_Backrest'):
  r[o.name]={'vertices':len(o.data.vertices),'polygons':len(o.data.polygons),'modifiers':[(m.name,m.type) for m in o.modifiers],'materials':[m.name for m in o.data.materials],'custom_normals':o.data.has_custom_normals}
(R/'brochure-source-audit.json').write_text(json.dumps(r,indent=2))
