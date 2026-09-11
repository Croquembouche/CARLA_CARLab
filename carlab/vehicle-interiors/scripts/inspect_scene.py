import bpy
bpy.ops.wm.open_mainfile(filepath='/mnt/simulations/vehicle-interiors/Lincoln_Interior_Mannequin.blend')
for o in bpy.context.scene.objects:
    if o.type=='MESH' and 'Door' in o.name:
        print(o.name,tuple(o.location),[(m.name if m else None) for m in o.data.materials],list(o.dimensions))
