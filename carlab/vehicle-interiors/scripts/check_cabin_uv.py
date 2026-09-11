import bpy,json
from pathlib import Path
r={};R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
m=bpy.data.objects['SK_LincolnMKZ.001'].data;r['source']={'layers':[x.name for x in m.uv_layers],'active':m.uv_layers.active.name,'render':[x.name for x in m.uv_layers if x.active_render]}
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(R/'exports/SM_MKZ_Cabin_Final.fbx'))
m=next(o for o in bpy.context.scene.objects if o.type=='MESH').data
r['export']={'layers':[x.name for x in m.uv_layers],'active':m.uv_layers.active.name,'render':[x.name for x in m.uv_layers if x.active_render],'uv_range':{x.name:[min(v.uv.x for v in x.data),max(v.uv.x for v in x.data),min(v.uv.y for v in x.data),max(v.uv.y for v in x.data)] for x in m.uv_layers}}
(R/'cabin-uv-audit.json').write_text(json.dumps(r,indent=2))
