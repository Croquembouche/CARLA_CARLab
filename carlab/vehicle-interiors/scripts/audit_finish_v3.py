import bpy,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
r=[]
for o in bpy.context.scene.objects:
 if o.type=='ARMATURE' or o.name=='SK_LincolnMKZ.001' or any(k in o.name.lower() for k in ['belt','mirror','visor']):
  d={'name':o.name,'type':o.type,'parent':o.parent.name if o.parent else None,'location':list(o.location),'dimensions':list(o.dimensions),'modifiers':[(m.name,m.type) for m in o.modifiers]}
  if o.type=='ARMATURE':d['bones']=[b.name for b in o.data.bones]
  if o.type=='MESH':d['materials']=[m.name if m else None for m in o.data.materials];d['groups']=[g.name for g in o.vertex_groups]
  r.append(d)
(R/'finish-v3-audit.json').write_text(json.dumps(r,indent=2))
