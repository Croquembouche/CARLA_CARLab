import bpy,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Baseline.blend'));o=bpy.data.objects['StockBody'];mesh=o.data
verts={v.index:o.matrix_world@v.co for v in mesh.vertices};selected={i for i,v in verts.items()if v.x>2 and .53<abs(v.y)<.86 and .4<v.z<.55}
adj={i:[]for i in selected}
for e in mesh.edges:
    a,b=e.vertices
    if a in selected and b in selected:adj[a].append(b);adj[b].append(a)
out=[]
while selected:
    stack=[selected.pop()];found=[]
    while stack:
        i=stack.pop();found.append(i)
        for j in adj[i]:
            if j in selected:selected.remove(j);stack.append(j)
    vv=[verts[i]for i in found];bounds=[[min(v[k]for v in vv),max(v[k]for v in vv)]for k in range(3)]
    if len(found)>8:out.append({'count':len(found),'bounds':bounds,'center':[(a+b)/2for a,b in bounds]})
(R/'reference/optics-probe.json').write_text(json.dumps(out,indent=2));print(json.dumps(out))
