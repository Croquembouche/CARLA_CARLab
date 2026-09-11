import bpy,json
from mathutils import Vector
from mathutils.bvhtree import BVHTree
bpy.ops.wm.open_mainfile(filepath='/mnt/simulations/vehicle-interiors/reference/lincoln-stock.blend')
o=next(o for o in bpy.context.scene.objects if o.type=='MESH')
vv=[o.matrix_world@v.co for v in o.data.vertices]; faces=[p.vertices[:] for p in o.data.polygons]
bvh=BVHTree.FromPolygons(vv,faces)
out={}
for y in [.36,.43]:
    for z in [.55,.7,1.0]:
        p=Vector((-1.6,y,z));hits=[]
        for i in range(40):
            loc,n,idx,d=bvh.ray_cast(p,Vector((1,0,0)),4)
            if loc is None:break
            hits.append(round(loc.x,3));p=loc+Vector((.002,0,0))
        out[f'x intersections y={y} z={z}']=hits
for x in [-.9,-.6,-.2,0,.2,.4,.6]:
    p=Vector((x,.4,1.35));hits=[]
    for i in range(30):
        loc,n,idx,d=bvh.ray_cast(p,Vector((0,0,-1)),2)
        if loc is None:break
        hits.append(round(loc.z,3));p=loc-Vector((0,0,.002))
    out[f'z intersections x={x}']=hits
print(json.dumps(out,indent=2))
