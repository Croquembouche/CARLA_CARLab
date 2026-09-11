import bpy,json
from pathlib import Path
bpy.ops.wm.open_mainfile(filepath='/mnt/simulations/vehicle-interiors/Lincoln_Interior_Mannequin.blend')
rows=[]
for o in bpy.context.scene.objects:
 if o.type!='MESH' or 'Lincol' not in o.name:continue
 rows.append({'name':o.name,'materials':[(i,m.name if m else None,sum(p.material_index==i for p in o.data.polygons)) for i,m in enumerate(o.data.materials)]})
print(json.dumps(rows,indent=2))
for m in bpy.data.materials:
 if 'Mannequin' in m.name:continue
 print(m.name,list(m.diffuse_color),[(n.type, getattr(n,'image',None).name if getattr(n,'image',None) else '') for n in m.node_tree.nodes] if m.use_nodes else '')
