import bpy,json,math
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'))
col=bpy.data.collections['Interior_Completion'];roof=bpy.data.objects['SM_MKZ_Moonroof_Glass']
r={'detail_count':len(col.objects),'detail_names':[o.name for o in col.objects],'moonroof_vertices':len(roof.data.vertices),'moonroof_transmission':roof.data.materials[0].node_tree.nodes.get('Principled BSDF').inputs['Transmission Weight'].default_value,'glazing_meshes':[o.name for o in bpy.data.objects if o.name.startswith('ReferenceGlazing_')],'finite_geometry':all(math.isfinite(c) for o in bpy.context.scene.objects if o.type=='MESH' for v in o.data.vertices for c in v.co),'unpacked_used_images':[i.name for i in bpy.data.images if i.source=='FILE' and i.has_data and i.users and not i.packed_file]}
print(json.dumps(r));assert r['finite_geometry'] and len(r['glazing_meshes'])>=5 and r['moonroof_transmission']>.5
(R/'completion-build.json').write_text(json.dumps(r,indent=2))

for o in bpy.data.objects:
 if o.name.startswith("ReferenceGlazing_"):print(o.name,len(o.data.vertices),len(o.data.polygons),list(o.dimensions))
