import bpy, math, json, bmesh
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
s=bpy.context.scene
old=bpy.data.collections.get('Interior_Final_Details')
if old:
 for o in list(old.objects):bpy.data.objects.remove(o,do_unlink=True)
col=bpy.data.collections.new('Interior_Completion');s.collection.children.link(col)
def move(o):
 for c in list(o.users_collection):c.objects.unlink(o)
 col.objects.link(o);return o
def mat(name,color,rough=.5,metal=0):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;return m
plastic=mat('MKZ_Seat_Base_Plastic',(.023,.025,.027));rubber=mat('MKZ_Completion_Rubber',(.009,.01,.012),.8);metal=mat('MKZ_Seat_Hardware',(.22,.24,.26),.23,.85);red=mat('MKZ_Belt_Release',(.42,.015,.012));web=mat('MKZ_Seatbelt_Webbing',(.043,.046,.049),.87);liner=mat('MKZ_Completion_Headliner',(.12,.125,.13),.95);lens=mat('MKZ_Completion_Lens',(.65,.67,.62),.25)
def box(name,loc,dims,ma=plastic,bevel=.006):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=move(bpy.context.object);o.name=name;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(ma);m=o.modifiers.new('Rounded edges','BEVEL');m.width=bevel;m.segments=3;o.modifiers.new('Normals','WEIGHTED_NORMAL');return o
def tube(name,pts,r=.008,ma=rubber):
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.bevel_depth=r;cu.bevel_resolution=3;sp=cu.splines.new('POLY');sp.points.add(len(pts)-1)
 for p,co in zip(sp.points,pts):p.co=(*co,1)
 o=bpy.data.objects.new(name,cu);col.objects.link(o);cu.materials.append(ma);return o
def belt(name,pts,width=.047):
 verts=[]
 for i,p in enumerate(pts):
  t=Vector(pts[min(i+1,len(pts)-1)])-Vector(pts[max(0,i-1)]);side=Vector((1,0,0)).cross(t).normalized()*width/2
  verts += [Vector(p)-side,Vector(p)+side]
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],[(2*i,2*i+1,2*i+3,2*i+2) for i in range(len(pts)-1)]);me.materials.append(web);o=bpy.data.objects.new(name,me);col.objects.link(o);m=o.modifiers.new('Webbing thickness','SOLIDIFY');m.thickness=.0015;o.modifiers.new('Soft edges','BEVEL').width=.0006;return o
for row,x,z in [('Front',-.27,1.25),('Rear',-1.27,1.16)]:
 for sign in (-1,1):
  n=f'{row}_{"Left" if sign==1 else "Right"}';y=sign*.625
  box(n+'_Height_Adjuster',(x-.01,y,z+.015),(.024,.075,.12),plastic)
  box(n+'_Upper_Belt_Guide',(x+.017,y,z),(.025,.061,.033),metal)
  box(n+'_Lower_Retractor',(x+.09,y,.48),(.09,.065,.115),plastic)
  if row=='Front' and sign==1:
   belt(n+'_Worn_Shoulder_Belt',[(x+.04,y,z),(.075,.565,1.26),(.275,.515,1.205),(.36,.41,1.08),(.39,.29,.94),(.35,.19,.78),(.26,.16,.65)])
   belt(n+'_Worn_Lap_Belt',[(.26,.16,.65),(.43,.26,.69),(.48,.40,.71),(.42,.54,.68),(.22,.62,.58)])
  else:
   belt(n+'_Stowed_Seatbelt',[(x+.025,y,z),(x+.07,y,.99),(x+.13,y,.76),(x+.10,y,.51)])
   box(n+'_Latch_Plate',(x+.145,y,.78),(.009,.065,.051),metal,.003)
  bx=.15 if row=='Front' else -.89
  by=sign*(.14 if row=='Front' else .205)
  box(n+'_Buckle_Stalk',(bx-.02,by,.59),(.035,.022,.10),rubber)
  box(n+'_Seatbelt_Buckle',(bx,by,.652),(.07,.045,.069),plastic)
  box(n+'_Buckle_Release',(bx+.036,by,.667),(.008,.031,.031),red,.003)
belt('Rear_Center_Visible_Belt',[(-1.18,.12,1.15),(-1.12,.12,1.02),(-1.06,.12,.84),(-.96,.12,.65)],.045)
box('Rear_Center_Belt_Guide',(-1.18,.12,1.15),(.04,.065,.025),plastic)
# Glass roof follows the measured roof arch and overlaps the existing aperture at its seals.
glass=mat('MKZ_Moonroof_Glass',(.11,.16,.18),.055);p=glass.node_tree.nodes.get('Principled BSDF');p.inputs['Transmission Weight'].default_value=.82;p.inputs['IOR'].default_value=1.48
verts=[];nx=24;ny=20
for i in range(nx+1):
 x=-.88+1.18*i/nx
 for j in range(ny+1):
  y=-.485+.97*j/ny;z=1.529-.039*((x+.35)/.65)**2-.05*(y/.6)**2;verts.append((x,y,z))
faces=[(i*(ny+1)+j,(i+1)*(ny+1)+j,(i+1)*(ny+1)+j+1,i*(ny+1)+j+1) for i in range(nx) for j in range(ny)]
me=bpy.data.meshes.new('Closed laminated moonroof');me.from_pydata(verts,[],faces);me.materials.append(glass);roof=bpy.data.objects.new('SM_MKZ_Moonroof_Glass',me);s.collection.objects.link(roof);sol=roof.modifiers.new('Laminated thickness','SOLIDIFY');sol.thickness=.004
for poly in me.polygons:poly.use_smooth=True
for y in [-.484,.484]:
 tube('Moonroof longitudinal rubber seal',[(x,y,1.529-.039*((x+.35)/.65)**2-.05*(y/.6)**2-.006) for x in [-.88+i*1.18/24 for i in range(25)]],.012)
 box('Moonroof sunshade guide',(-.29,y,1.454),(1.15,.018,.023),plastic)
for x in [-.876,.296]:tube('Moonroof end seal',[(x,y,1.529-.039*((x+.35)/.65)**2-.05*(y/.6)**2-.006) for y in [-.484+i*.968/20 for i in range(21)]],.013)
box('Retracted sunshade leading handle',(-.84,0,1.472),(.045,.29,.023),liner)
# Roof equipment sits beneath the factory roof frame.
box('Overhead console housing',(.34,0,1.435),(.21,.25,.038),liner,.018)
for sign in [-1,1]:
 box('Map light lens',(.35,sign*.078,1.413),(.08,.055,.009),lens)
 box('Roof switch',(.29,sign*.033,1.410),(.029,.023,.011),plastic,.003)
 visor=box('Sun visor',(.48,sign*.36,1.398),(.14,.32,.033),liner,.016);visor.rotation_euler.y=-.18
 box('Visor vanity cover',(.467,sign*.36,1.378),(.09,.19,.006),plastic,.006)
 for x in [.035,-.79]:
  tube('Passenger grab handle',[(x-.095,sign*.596,1.387),(x-.078,sign*.566,1.364),(x+.078,sign*.566,1.364),(x+.095,sign*.596,1.387)],.013,liner)
  for dx in [-.095,.095]:box('Grab handle hinge',(x+dx,sign*.596,1.387),(.04,.045,.028),liner)
# Rear console face and outlets.
box('Rear console vent surround',(-.27,0,.73),(.045,.21,.15),plastic,.012)
for sign in [-1,1]:
 box('Rear console vent recess',(-.296,sign*.055,.755),(.01,.082,.068),rubber,.004)
 for dz in [-.021,-.007,.007,.021]:box('Rear vent horizontal louver',(-.305,sign*.055,.755+dz),(.014,.077,.006),plastic,.002)
 box('Rear USB surround',(-.301,sign*.038,.679),(.012,.033,.019),metal,.003)
 box('Rear USB port',(-.309,sign*.038,.679),(.004,.024,.009),rubber,.001)
box('Rear power socket cover',(-.304,0,.626),(.014,.075,.043),plastic)
for sign in [-1,1]:
 pocket=box('Front seatback storage pocket',(-.17,sign*.373,.79),(.045,.32,.21),plastic,.018);pocket.rotation_euler.y=-.2
 tube('Seatback pocket upper welt',[(-.198,sign*.373+dy,.895) for dy in [-.15,-.075,0,.075,.15]],.006,rubber)
# Bring the original glazing into the editable Blender assembly (the vehicle already has these in UE).
for f in (R/'reference/glazing').glob('*.fbx'):
 if f.stem=='SM_LincolnMKZ_Glass_Ext2':continue
 before=set(s.objects);bpy.ops.import_scene.fbx(filepath=str(f))
 for o in set(s.objects)-before:
  if o.type=='MESH':
   o.data.materials.clear();o.data.materials.append(glass);o.name='ReferenceGlazing_'+f.stem
s.camera=bpy.data.objects['MKZ_Dashboard'];bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'))
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
def export(objects,name):
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 bpy.ops.export_scene.fbx(filepath=str(R/'exports'/(name+'.fbx')),use_selection=True,object_types={'MESH','OTHER'},bake_anim=False,axis_forward='-Y',axis_up='Z')
# Convert curves only in export scene after editable save.
bpy.ops.object.select_all(action='DESELECT')
for o in col.objects:o.select_set(True)
bpy.context.view_layer.objects.active=list(col.objects)[0];bpy.ops.object.convert(target='MESH')
export(list(col.objects),'SM_MKZ_Completion_Details');export([roof],'SM_MKZ_Moonroof_Glass')
car=bpy.data.objects['SK_LincolnMKZ.001'];cabin=car.copy();cabin.data=car.data.copy();s.collection.objects.link(cabin)
bm=bmesh.new();bm.from_mesh(cabin.data);bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index!=2],context='FACES');bm.to_mesh(cabin.data);bm.free()
objects=[cabin]+[o for o in s.objects if (o.name.startswith(('Front_Left_','Front_Right_','Rear_Left_','Rear_Right_','Rear_Center_')) or o.name=='SM_LincolnCabinDetails') and o.name not in col.objects]
bpy.ops.object.select_all(action='DESELECT')
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=cabin;bpy.ops.object.convert(target='MESH');bpy.ops.object.join();cabin=bpy.context.object;s.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');export([cabin],'SM_MKZ_Cabin_Final')
(R/'completion-build.json').write_text(json.dumps({'details':len(col.objects),'glass_vertices':len(me.vertices),'blend':str(R/'Lincoln_MKZ_Interior_Completed.blend')},indent=2))
