import bpy, json
from pathlib import Path
from mathutils import Vector
root = Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(root/'reference/SK_LincolnMKZ.fbx'))
report=[]
for o in bpy.context.scene.objects:
    if o.type == 'MESH':
        verts=[o.matrix_world@v.co for v in o.data.vertices]
        entry={'name':o.name, 'bounds':[[min(v[i] for v in verts), max(v[i] for v in verts)] for i in range(3)], 'materials':[]}
        for j,m in enumerate(o.data.materials):
            inds={i for p in o.data.polygons if p.material_index==j for i in p.vertices}
            vv=[o.matrix_world@o.data.vertices[i].co for i in inds]
            entry['materials'].append({'name':m.name if m else None,'vertices':len(vv), 'bounds': [[min(v[i] for v in vv),max(v[i] for v in vv)] for i in range(3)] if vv else []})
        report.append(entry)
    elif o.type == 'ARMATURE':
        report.append({'name':o.name,'bones': [{'name':b.name,'head':list(o.matrix_world@b.head_local)} for b in o.data.bones]})
(root/'reference/blender-audit.json').write_text(json.dumps(report,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(root/'reference/lincoln-stock.blend'))
