"""Add front-door speaker grilles and padded armrests, exported separately for moving doors."""
import bpy,math,json
from mathutils import Vector
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));s=bpy.context.scene
def material(name,c,rough,metal=0):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;return m
metal=material('MKZ_Speaker_Metal',(.13,.145,.16),.46,.72);black=bpy.data.materials['MKZ_Vent_Recess'];satin=bpy.data.materials['MKZ_Fine_Satin'];leather=bpy.data.materials['MKZ_Ebony_Leather'];plastic=bpy.data.materials['MKZ_Seat_Base_Plastic'];exports={}
for code,side in [('FL',1),('FR',-1)]:
 col=bpy.data.collections.get('DoorDetail_'+code)
 if col:
  for o in list(col.objects):bpy.data.objects.remove(o,do_unlink=True)
 else:col=bpy.data.collections.new('DoorDetail_'+code);s.collection.children.link(col)
 door=bpy.data.objects['SM_LincolnMKZ_Door_'+code];ev=door.evaluated_get(bpy.context.evaluated_depsgraph_get());inv=ev.matrix_world.inverted()
 def surface(x,z,offset):
  hit,p,n,f=ev.ray_cast(inv@Vector((x,0,z)),(inv.to_3x3()@Vector((0,side,0))).normalized());assert hit,(code,x,z)
  p=ev.matrix_world@p;p.y-=side*offset;return p
 def mesh(name,vs,fs,m):
  me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.materials.append(m);me.update()
  for f in me.polygons:
   if f.normal.y*side>0:f.flip()
   f.use_smooth=True
  o=bpy.data.objects.new('DoorDetail_'+code+'_'+name,me);col.objects.link(o);world=o.matrix_world.copy();o.parent=door;o.matrix_world=world;return o
 def cap(name,cx,cz,rx,rz,offset,m):
  vs=[surface(cx,cz,offset+.002)];fs=[];n=80;nr=6
  for r in range(1,nr+1):
   q=r/nr
   for j in range(n):
    t=2*math.pi*j/n;vs.append(surface(cx+rx*q*math.cos(t),cz+rz*q*math.sin(t),offset+.002*(1-q*q)))
  for j in range(n):fs.append((0,1+j,1+(j+1)%n))
  for r in range(nr-1):
   for j in range(n):
    a=1+r*n+j;b=1+r*n+(j+1)%n;fs.append((a,b,b+n,a+n))
  return mesh(name,vs,fs,m)
 def tube(name,pts,r,m):
  cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.bevel_depth=r;cu.bevel_resolution=3;sp=cu.splines.new('POLY');sp.points.add(len(pts)-1)
  for p,v in zip(sp.points,pts):p.co=(*v,1)
  sp.use_cyclic_u=True;o=bpy.data.objects.new('DoorDetail_'+code+'_'+name,cu);col.objects.link(o);cu.materials.append(m);world=o.matrix_world.copy();o.parent=door;o.matrix_world=world
 door_surface=surface
 for label,cx,cz,rx,rz,pitch in [('Tweeter',.806,.914,.031,.031,.0028),('Lower_Speaker',.745,.690,.064,.073,.0035)]:
  rim_samples=[door_surface(cx+(rx+.007)*math.cos(2*math.pi*j/80),cz+(rz+.007)*math.sin(2*math.pi*j/80),0) for j in range(80)]
  plane_y=side*min(side*p.y for p in rim_samples)
  def surface(x,z,offset):return Vector((x,plane_y-side*offset,z))
  cap(label+'_Housing',cx,cz,rx+.007,rz+.007,.003,plastic);cap(label+'_Grille',cx,cz,rx,rz,.005,metal)
  tube(label+'_Bezel',[surface(cx+rx*math.cos(2*math.pi*j/100),cz+rz*math.sin(2*math.pi*j/100),.006) for j in range(100)],.0015,satin)
  vs=[];fs=[];nx=int(rx/pitch);nz=int(rz/pitch)
  for iz in range(-nz,nz+1):
   for ix in range(-nx,nx+1):
    dx=(ix+.5*(iz%2))*pitch;dz=iz*pitch;q=(dx/rx)**2+(dz/rz)**2
    if q>.88:continue
    off=.005+.002*(1-q)+.0002;start=len(vs)
    for j in range(6):
     t=2*math.pi*j/6;vs.append(surface(cx+dx+.00065*math.cos(t),cz+dz+.00065*math.sin(t),off))
    fs.append(tuple(range(start,start+6)))
  mesh(label+'_Perforations',vs,fs,black)
  rim_front=[surface(p.x,p.z,.003) for p in rim_samples];mesh(label+'_Housing_Skirt',rim_front+rim_samples,[(j,(j+1)%80,(j+1)%80+80,j+80) for j in range(80)],plastic)
  surface=door_surface
 # A padded armrest forms a continuous ledge below the upper trim; switch bezel at its front.
 def box(name,x,z,dx,dy,dz,m,bevel):
  p=surface(x,z,dy*.55);bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.name='DoorDetail_'+code+'_'+name;o.dimensions=(dx,dy,dz);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
  for c in list(o.users_collection):c.objects.unlink(o)
  col.objects.link(o);o.data.materials.append(m);be=o.modifiers.new('Rounded padded edge','BEVEL');be.width=bevel;be.segments=6;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');world=o.matrix_world.copy();o.parent=door;o.matrix_world=world;return o
 box('Padded_Armrest',.23,.735,.56,.075,.043,leather,.019)
 box('Window_Switch_Bezel',.50,.751,.11,.047,.006,plastic,.006)
 for j in range(2):box('Window_Rocker_'+str(j),.478+j*.034,.756,.026,.022,.004,black,.002)
 exports[code]=[o.name for o in col.objects]
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
for code,names in exports.items():
 bpy.ops.object.select_all(action='DESELECT')
 for name in names:bpy.data.objects[name].select_set(True)
 bpy.context.view_layer.objects.active=bpy.data.objects[names[0]];bpy.ops.export_scene.fbx(filepath=str(R/'exports'/('SM_MKZ_Door_Details_'+code+'.fbx')),use_selection=True,object_types={'MESH','OTHER'},bake_anim=False,axis_forward='-Y',axis_up='Z')
(R/'brochure-doors-v2.json').write_text(json.dumps({'result':'SOURCE_SAVED_FOR_OPEN_DOOR_CHECK','objects':exports,'attachment':'separate child mesh of each matching front door, with inverse skeleton-hinge offset in Unreal'},indent=2))
