"""Measured vent geometry, inset console lid and tailored upholstery seams."""
import bpy,math,json,shutil
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));s=bpy.context.scene
backup=R/'backups/before-brochure-details-v2.blend'
if not backup.exists():shutil.copy2(R/'Lincoln_MKZ_Interior_Brochure.blend',backup)
col=bpy.data.collections.get('Brochure_Details_V2')
if col:
 for o in list(col.objects):bpy.data.objects.remove(o,do_unlink=True)
else:col=bpy.data.collections.new('Brochure_Details_V2');s.collection.children.link(col)
def mat(name,c,r,metal=0):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=r;p.inputs['Metallic'].default_value=metal;return m
black=mat('MKZ_Vent_Recess',(.003,.004,.005),.72);slat=mat('MKZ_Vent_Louver',(.014,.017,.021),.38);satin=mat('MKZ_Fine_Satin',(.22,.235,.25),.43,.85);leather=bpy.data.materials['MKZ_Ebony_Leather'];seammat=mat('MKZ_Seam_Shadow',(.0025,.003,.0035),.78);thread=bpy.data.materials['MKZ_Stitch_Charcoal']
def link(o):
 for c in list(o.users_collection):c.objects.unlink(o)
 col.objects.link(o);return o
def box(name,loc,dims,m,bevel=.002):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=link(bpy.context.object);o.name='Brochure_V2_'+name;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m);b=o.modifiers.new('Machined radius','BEVEL');b.width=bevel;b.segments=4;o.modifiers.new('Surface normals','WEIGHTED_NORMAL')
 for p in o.data.polygons:p.use_smooth=True
 return o
def line(name,pts,r,m,closed=False):
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.resolution_u=2;cu.bevel_depth=r;cu.bevel_resolution=3;sp=cu.splines.new('POLY');sp.points.add(len(pts)-1)
 for p,v in zip(sp.points,pts):p.co=(*v,1)
 sp.use_cyclic_u=closed;o=bpy.data.objects.new('Brochure_V2_'+name,cu);col.objects.link(o);cu.materials.append(m);return o
# Work against evaluated stock surfaces, independent of the new details.
dg=bpy.context.evaluated_depsgraph_get();surfaces=[o.evaluated_get(dg) for o in s.objects if o.type=='MESH' and (o.name=='SK_LincolnMKZ.001' or o.name.startswith('Brochure_Dashboard'))]
def surf(y,z,offset=.004):
 hits=[]
 for o in surfaces:
  inv=o.matrix_world.inverted();hit,p,n,f=o.ray_cast(inv@Vector((.65,y,z)),(inv.to_3x3()@Vector((1,0,0))).normalized())
  if hit:hits.append((o.matrix_world@p).x)
 assert hits,(y,z)
 return Vector((min(hits)-offset,y,z))
def face(name,yz,m,offset=.003):
 me=bpy.data.meshes.new(name);me.from_pydata([surf(y,z,offset) for y,z in yz],[],[tuple(range(len(yz)))]);me.materials.append(m);me.update();
 if me.polygons[0].normal.x>0:me.polygons[0].flip()
 o=bpy.data.objects.new('Brochure_V2_'+name,me);col.objects.link(o);return o
# Center vent: black cavity, three gently raked blades and a silver center adjustment divider.
face('Center_Vent_Recess',[(-.078,.904),(.078,.904),(.073,.937),(-.073,.937)],black)
for i,z in enumerate([.911,.920,.929]):
 width=.075-(z-.91)*.08
 for side in [-1,1]:
  pts=[surf(side*(.004+(width-.004)*j/20),z,.009) for j in range(21)]
  line('Center_Louver_'+str(i)+'_'+str(side),pts,.0016,slat)
line('Center_Vent_Divider',[surf(0,.905+j*.031/20,.011) for j in range(21)],.0021,satin)
for side in [-1,1]:
 line('Center_Adjuster_'+str(side),[surf(side*.035,.914+j*.010/8,.013) for j in range(9)],.0023,satin)
# Side vent shapes measured from the unlit dashboard projection. Outboard corners follow the tapered stock bezel.
for side in [-1,1]:
 inner=[(.621,.866),(.660,.883),(.759,.881),(.762,.852),(.678,.852)]
 face('Side_Vent_Recess_'+str(side),[(side*y,z) for y,z in inner],black,.003)
 for i,z in enumerate([.858,.866,.874]):
  ymin=.649 if i==1 else .672
  line('Side_Louver_'+str(side)+'_'+str(i),[surf(side*(ymin+(.753-ymin)*j/24),z,.008) for j in range(25)],.0017,slat)
 line('Side_Adjuster_'+str(side),[surf(side*.704,.862+j*.010/8,.012) for j in range(9)],.0025,satin)
 # A slim physical inner rim creates changing highlights rather than a white texture outline.
 line('Side_Vent_Inner_Rim_'+str(side),[surf(side*y,z,.006) for y,z in inner],.0012,satin,True)
# Replace the oversized single lid and transverse handle with two fitted satin panels and a recessed longitudinal pull.
for name in ['Brochure_Console_Sliding_Cover','Brochure_Console_Cover_Finger_Pull']:
 o=bpy.data.objects.get(name)
 if o:bpy.data.objects.remove(o,do_unlink=True)
box('Console_Inset_Gasket',(.329,0,.704),(.338,.215,.007),black,.006)
box('Console_Main_Lid',(.295,0,.708),(.262,.198,.005),satin,.004)
box('Console_Front_Lid',(.462,0,.708),(.066,.198,.005),satin,.003)
box('Console_Pull_Recess',(.276,-.074,.7112),(.050,.009,.0018),black,.004)
box('Console_Pull_Inner',(.276,-.074,.7123),(.041,.0025,.001),satin,.001)
# Gasket and matching stitch around the armrest crown.
pts=[]
for i in range(129):
 t=2*math.pi*i/128;x=-.055+.155*math.copysign(abs(math.cos(t))**.25,math.cos(t));y=.096*math.copysign(abs(math.sin(t))**.25,math.sin(t));pts.append((x,y,.7448))
line('Armrest_Edge_Stitch',pts,.00055,thread,True)
# Upholstery insert borders and narrow center bindings follow the existing mesh grid exactly.
for prefix in ['Front_Left','Front_Right','Rear_Left','Rear_Right']:
 for kind in ['Backrest','Cushion']:
  o=bpy.data.objects[prefix+'_Perforated_'+kind];vs=o.data.vertices;n=round(math.sqrt(len(vs)));assert n*n==len(vs),(o.name,n,len(vs))
  off=Vector((.0018,0,0)) if kind=='Backrest' else Vector((0,0,.0018))
  def point(i,j):return o.matrix_world@vs[j*n+i].co+off
  perimeter=[point(i,0) for i in range(n)]+[point(n-1,j) for j in range(1,n)]+[point(i,n-1) for i in range(n-2,-1,-1)]+[point(0,j) for j in range(n-2,0,-1)]
  line(prefix+'_'+kind+'_Panel_Welt',perimeter,.00105,seammat,True)
  for i in [n//2-1,n//2+1]:
   line(prefix+'_'+kind+'_Center_Binding_'+str(i),[point(i,j) for j in range(n)],.0009,seammat)
 # Headrest front-face seam follows actual convex geometry with a ray-cast.
 h=bpy.data.objects[prefix+'_Headrest'];ev=h.evaluated_get(dg);inv=ev.matrix_world.inverted();coords=[h.matrix_world@Vector(p) for p in h.bound_box];center=sum(coords,Vector())/8;hy=(max(p.y for p in coords)-min(p.y for p in coords))/2;hz=(max(p.z for p in coords)-min(p.z for p in coords))/2;pts=[]
 for j in range(128):
  t=2*math.pi*j/128;y=center.y+.88*hy*math.copysign(abs(math.cos(t))**.55,math.cos(t));z=center.z+.88*hz*math.copysign(abs(math.sin(t))**.55,math.sin(t));hit,p,n,f=ev.ray_cast(inv@Vector((1.1,y,z)),(inv.to_3x3()@Vector((-1,0,0))).normalized())
  if hit:pts.append(ev.matrix_world@p+Vector((.0006,0,0)))
 assert len(pts)>100
 line(prefix+'_Headrest_Seam',pts,.0006,thread,True)
# Rear center armrest outline: closed in the seat back, with a small pull tab.
o=bpy.data.objects['Rear_Center_Backrest'];ev=o.evaluated_get(dg);inv=ev.matrix_world.inverted();pts=[]
for y,z in [(-.116,.70),(-.116,1.00),(.116,1.00),(.116,.70)]:
 hit,p,n,f=ev.ray_cast(inv@Vector((0,y,z)),(inv.to_3x3()@Vector((-1,0,0))).normalized());assert hit;pts.append(ev.matrix_world@p+Vector((.001,0,0)))
line('Rear_Center_Armrest_Seam',pts,.0013,seammat,True)
box('Rear_Center_Armrest_Pull',(-1.153,0,1.002),(.005,.037,.013),leather,.003)
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
(R/'brochure-details-v2.json').write_text(json.dumps({'result':'SOURCE_SAVED_FOR_CARLA_REVIEW','objects':[o.name for o in col.objects],'materials':[m.name for m in [black,slat,satin,seammat]],'changes':['physical center and side vent louvers','split console lid with recessed pull','tailored insert bindings and headrest seams','rear center armrest outline']},indent=2))
exec(compile((R/'scripts/export_completed_cabin.py').read_text(),'export_completed_cabin.py','exec'))
