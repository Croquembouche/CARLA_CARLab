import bpy,json,numpy as np,hashlib
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Baseline.blend'));o=bpy.data.objects['StockBody'];old=np.array([list(o.matrix_world@v.co)for v in o.data.vertices]);oldfaces=len(o.data.polygons)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(R/'reference/SK_LincolnMKZ.fbx'));o=next(o for o in bpy.context.scene.objects if o.type=='MESH'and not o.name.startswith('UCX'));new=np.array([list(o.matrix_world@v.co)for v in o.data.vertices]);faces=len(o.data.polygons)
a=np.unique(np.round(old,5),axis=0);b=np.unique(np.round(new,5),axis=0);same=a.shape==b.shape and np.array_equal(a,b)
assert same,'Fresh stock mesh geometry differs from the cached stock export';assert oldfaces==faces
report={'status':'PASS_FRESH_SOURCE_GEOMETRY_EQUIVALENCE','baseline_source':'Existing stock FBX exported September 9 from the unchanged native mesh; fresh export verified on October 4.','baseline_vertices':len(old),'fresh_vertices':len(new),'faces':faces,'unique_positions':len(a),'position_precision_m':1e-5,'same_unique_positions':same,'fresh_fbx_sha256':hashlib.sha256((R/'reference/SK_LincolnMKZ.fbx').read_bytes()).hexdigest()}
(R/'reference/fresh-mesh-verification.json').write_text(json.dumps(report,indent=2));print(report)
