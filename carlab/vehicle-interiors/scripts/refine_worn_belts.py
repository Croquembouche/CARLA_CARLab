import bpy
from mathutils import Vector
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'))
def ribbon(name,points,normal):
 o=bpy.data.objects[name];ps=[Vector(p) for p in points];curve=[]
 for k in range(len(ps)-1):
  a=ps[max(k-1,0)];b=ps[k];c=ps[k+1];d=ps[min(k+2,len(ps)-1)]
  for j in range(16):
   t=j/16;curve.append(.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t))
 curve.append(ps[-1]);verts=[];prev=None
 body=bpy.data.objects['SK_SeatedMannequin'].evaluated_get(bpy.context.evaluated_depsgraph_get());inv=body.matrix_world.inverted()
 for i,p in enumerate(curve):
  if name.endswith('Lap_Belt'):continue
  hit,loc,n,idx=body.ray_cast(inv@Vector((1.2,p.y,p.z)),(inv.to_3x3()@Vector((-1,0,0))).normalized())
  if hit:p.x=(body.matrix_world@loc).x+.010
 # Smooth measured body contour without changing the belt endpoints.
 for _ in range(3):
  xs=[p.x for p in curve]
  for i in range(2,len(curve)-2):curve[i].x=max(xs[i],sum(xs[i-2:i+3])/5)

 for i,p in enumerate(curve):
  tangent=(curve[min(i+1,len(curve)-1)]-curve[max(i-1,0)]).normalized()
  n=Vector((1,0,0))
  if name.endswith('Shoulder_Belt'):
   f=min(1,i/32);n=Vector((f,0,1-f)).normalized()
  w=n.cross(tangent).normalized()
  if prev is not None and w.dot(prev)<0:w=-w
  prev=w;verts.extend([p-w*.0235,p+w*.0235])
 me=bpy.data.meshes.new(name+' smooth webbing');me.from_pydata(verts,[],[(2*i,2*i+1,2*i+3,2*i+2) for i in range(len(curve)-1)]);me.materials.append(bpy.data.materials['MKZ_Seatbelt_Webbing']);o.data=me;o.matrix_world.identity()
 o.modifiers.clear();m=o.modifiers.new('Webbing thickness','SOLIDIFY');m.thickness=.0015;m=o.modifiers.new('Soft woven edge','BEVEL');m.width=.0006;m.segments=2
 for p in me.polygons:p.use_smooth=True
ribbon('Front_Left_Worn_Lap_Belt',[(.185,.14,.66),(.25,.20,.80),(.315,.28,.82),(.315,.40,.82),(.315,.52,.82),(.28,.58,.78),(.22,.625,.57)],(1,0,1))
ribbon('Front_Left_Worn_Shoulder_Belt',[(-.24,.625,1.25),(-.08,.57,1.18),(.275,.515,1.15),(.36,.41,1.06),(.39,.29,.93),(.35,.19,.78),(.185,.14,.66)],(1,0,0))
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/('Lincoln_MKZ_Interior_'+name+'.blend')))
bpy.ops.object.select_all(action='DESELECT')
for o in bpy.data.collections['Interior_Completion'].objects:
 if '_Worn_' in o.name:o.select_set(True)
bpy.ops.export_scene.fbx(filepath=str(R/'exports/SM_MKZ_Driver_Worn_Belt.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')
