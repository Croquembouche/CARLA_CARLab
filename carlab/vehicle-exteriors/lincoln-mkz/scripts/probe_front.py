import bpy,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Baseline.blend'))
o=bpy.data.objects['StockBody'];tree=BVHTree.FromObject(o,bpy.context.evaluated_depsgraph_get());inv=o.matrix_world.inverted();rows=[]
for z in [.35,.4,.45,.5,.55,.6,.65,.7,.75,.8,.85,.9]:
    for y in [0,.15,.3,.45,.6,.75,.85]:
        p,n,i,d=tree.ray_cast(inv@Vector((3,y,z)),(inv.to_3x3()@Vector((-1,0,0))).normalized())
        if p is not None:
            wp=o.matrix_world@p;poly=o.data.polygons[i];uv=[list(o.data.uv_layers.active.data[l].uv)for l in poly.loop_indices];rows.append({'y':y,'z':z,'x':wp.x,'material':o.data.materials[poly.material_index].name,'uv':uv})
(R/'reference/front-probe.json').write_text(json.dumps(rows,indent=2))
