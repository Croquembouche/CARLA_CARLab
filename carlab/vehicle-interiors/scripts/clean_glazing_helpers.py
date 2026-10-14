import bpy
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'))
for o in list(bpy.data.objects):
 if o.type=='MESH' and o.name.startswith('ReferenceGlazing_') and len(o.data.vertices)<100:bpy.data.objects.remove(o,do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
