import bpy
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath='/mnt/simulations/vehicle-interiors/Lincoln_Interior_Mannequin.blend')
o=bpy.data.objects['SK_LincolnMKZ.001'];m=o.data
for name,pt in [('seat',(0,.4,1)),('dash',(.85,-.4,.95)),('roof',(0,0,1.47))]:
 p=min(m.polygons,key=lambda p:(o.matrix_world@p.center-Vector(pt)).length)
 print(name,p.index,p.material_index,p.center[:])
 for layer in m.uv_layers:print(layer.name,layer.data[p.loop_start].uv[:])
