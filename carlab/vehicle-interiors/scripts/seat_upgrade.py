"""Replace stock low-detail seating with contoured upholstery for close cameras.
Dimensions follow the stock cabin envelope; visual panel layout follows MKZ brochure.
Executed inside refine_lincoln.py, with its scene/material helpers available.
"""
import bmesh
car=bpy.data.objects['SK_LincolnMKZ.001'];bm=bmesh.new();bm.from_mesh(car.data)
remove=[]
for f in bm.faces:
 if f.material_index!=2:continue
 coords=[car.matrix_world@v.co for v in f.verts]
 def inside(c):
  front=(-.35<c.x<.55 and .112<abs(c.y)<.65 and .48<c.z<1.43 and (c.z<.70 or c.x<.15))
  rear=(-1.40<c.x<-.54 and abs(c.y)<.65 and .48<c.z<1.35)
  return front or rear
 if all(inside(c) for c in coords):remove.append(f)
bmesh.ops.delete(bm,geom=remove,context='FACES');bm.to_mesh(car.data);bm.free()
# Remove previous temporary stitch lines; the new seams follow the new surfaces.
for o in list(scene.objects):
 if o.type=='CURVE' and 'Stitch' in o.name:bpy.data.objects.remove(o,do_unlink=True)
seat_objects=[]
insert=mat('MKZ_Perforated_Leather',(.016,.019,.022),.48,grain=.00008)
plastic=mat('MKZ_Seat_Base_Plastic',(.012,.014,.018),.68,grain=.00008)
chrome=mat('MKZ_Seat_Hardware',(.5,.53,.57),.23,.85)
redmat=mat('MKZ_Belt_Release',(.35,.008,.007),.5)
# Fine perforation pattern in upholstery UV coordinates.
nt=insert.node_tree;p=nt.nodes.get('Principled BSDF');uv=nt.nodes.new('ShaderNodeTexCoord');scale=nt.nodes.new('ShaderNodeVectorMath');scale.operation='MULTIPLY';scale.inputs[1].default_value=(80,120,1);nt.links.new(uv.outputs['UV'],scale.inputs[0]);v=nt.nodes.new('ShaderNodeTexVoronoi');v.distance='EUCLIDEAN';v.inputs['Scale'].default_value=1;v.inputs['Randomness'].default_value=0;nt.links.new(scale.outputs[0],v.inputs['Vector']);r=nt.nodes.new('ShaderNodeValToRGB');r.color_ramp.elements[0].position=.04;r.color_ramp.elements[0].color=(.005,.006,.007,1);r.color_ramp.elements[1].position=.13;r.color_ramp.elements[1].color=(.018,.021,.024,1);nt.links.new(v.outputs['Distance'],r.inputs[0]);nt.links.new(r.outputs[0],p.inputs['Base Color'])

def mesh_grid(name,fn,nu=36,nv=36,material=leather,thickness=.03):
 verts=[fn(i/nu,j/nv) for j in range(nv+1) for i in range(nu+1)];faces=[]
 for j in range(nv):
  for i in range(nu):
   a=j*(nu+1)+i;faces.append((a,a+1,a+nu+2,a+nu+1))
 if 'Cushion' in name:faces=[tuple(reversed(f)) for f in faces]
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.materials.append(material);me.update()
 ob=bpy.data.objects.new(name,me);scene.collection.objects.link(ob);seat_objects.append(ob)
 uv=me.uv_layers.new(name='UpholsteryUV')
 for face in me.polygons:
  face.use_smooth=True
  for li in face.loop_indices:
   vi=me.loops[li].vertex_index;uv.data[li].uv=(vi%(nu+1)/nu,vi//(nu+1)/nv)
 if thickness:
  sol=ob.modifiers.new('Upholstery thickness','SOLIDIFY');sol.thickness=thickness;sol.offset=-1
  bev=ob.modifiers.new('Soft sewn edges','BEVEL');bev.width=.009;bev.segments=3
 return ob

def rounded_box(name,loc,size,material,radius=.035):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);ob=bpy.context.object;ob.name=name;ob.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);ob.data.materials.append(material)
 bevel=ob.modifiers.new('Rounded upholstery','BEVEL');bevel.width=radius;bevel.segments=6
 for face in ob.data.polygons:face.use_smooth=True
 ob.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL');seat_objects.append(ob);return ob

def pad(name,loc,scale,tilt=0):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=40,ring_count=24,location=loc);ob=bpy.context.object;ob.name=name;ob.scale=scale;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);ob.rotation_euler.y=tilt;ob.data.materials.append(leather)
 for face in ob.data.polygons:face.use_smooth=True
 seat_objects.append(ob);return ob

def seamline(name,fn,t0=0,t1=1):
 pts=[fn(t0+(t1-t0)*i/120) for i in range(121)]
 for j in range(0,120,2):line(name,pts[j:j+2],.00035)

for row,xbase,zmax in [('Front',0,1.17),('Rear',-1.035,1.10)]:
 for side in [-1,1]:
  cy=side*.373;prefix=row+('_Left' if side==1 else '_Right')
  def back(u,v):
   z=.64+(zmax-.64)*v
   width=(.242-.025*v+.008*math.sin(math.pi*v))*(1-.09*math.exp(-(v/.05)**2)-.11*math.exp(-((v-1)/.055)**2))
   y=cy+(u*2-1)*width
   x=xbase+.055-.24*v+.030*math.exp(-((v-.22)/.24)**2)+.072*math.exp(-((abs(2*u-1)-.80)/.22)**2)*math.sin(math.pi*v)**.35+.018*math.exp(-((v-.91)/.09)**2)
   return (x,y,z)
  mesh_grid(prefix+'_Backrest',back,material=leather,thickness=.075)
  def inset(u,v):
   # Inset panel sits within rounded outer shoulder and lumbar bolsters.
   vv=.10+.79*v;uu=.19+.62*u;x,y,z=back(uu,vv)
   x+=.003+.0015*math.cos(v*math.pi*6)
   return (x,y,z)
  mesh_grid(prefix+'_Perforated_Backrest',inset,material=insert,thickness=.003)
  for edge in [.025,.975]:
   seamline(prefix+'_Back_Stitch',lambda t,e=edge:tuple(Vector(inset(e,t))+Vector((.003,0,0))))
  for edge in [.475,.525]:seamline(prefix+'_Center_Stitch',lambda t,e=edge:tuple(Vector(inset(e,t))+Vector((.003,0,0))))
  def cushion(u,v):
   x=xbase-.045+.53*v;y=cy+(u*2-1)*(.243-.01*v)
   z=.575+.045*math.exp(-((abs(2*u-1)-.80)/.22)**2)*math.sin(math.pi*v)**.35+.026*math.sin(math.pi*v)-.016*v
   return (x,y,z)
  mesh_grid(prefix+'_Cushion',cushion,material=leather,thickness=.065)
  def cinset(u,v):
   x,y,z=cushion(.19+.62*u,.09+.81*v);return(x,y,z+.003)
  mesh_grid(prefix+'_Perforated_Cushion',cinset,material=insert,thickness=.003)
  for edge in [.025,.475,.525,.975]:seamline(prefix+'_Cushion_Stitch',lambda t,e=edge:tuple(Vector(cinset(e,t))+Vector((0,0,.003))))
  rounded_box(prefix+'_Base',(xbase+.19,cy,.505),(.53,.46,.10),plastic,.035)
  hx=xbase-.20;hz=zmax+.125
  rounded_box(prefix+'_Headrest',(hx,cy,hz),(.14,.265,.225),leather,.048)
  for dy in [-.065,.065]:
   bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=.007,depth=.09,location=(hx,cy+dy,zmax+.008));ob=bpy.context.object;ob.name=prefix+'_Headrest_Post';ob.data.materials.append(chrome);seat_objects.append(ob)
  # Buckle at the inboard edge, outside the seat surface.
  by=cy-side*.265
  rounded_box(prefix+'_Seatbelt_Buckle',(xbase+.09,by,.605),(.065,.032,.075),plastic,.009)
  rounded_box(prefix+'_Red_Release',(xbase+.09,by,.645),(.045,.025,.008),redmat,.003)
# Rear center seat remains upright, with separate backrest and cushion.
rounded_box('Rear_Center_Backrest',(-1.18,0,.86),(.15,.27,.50),leather,.048)
rounded_box('Rear_Center_Cushion',(-.85,0,.56),(.49,.28,.09),leather,.04)
rounded_box('Rear_Center_Headrest',(-1.23,0,1.185),(.13,.22,.18),leather,.04)
# Export the new seat module independently for the subsequent skeletal-mesh integration.
bpy.ops.object.select_all(action='DESELECT')
for ob in seat_objects:ob.select_set(True)
for ob in scene.objects:
 if ob.type=='CURVE' and 'Stitch' in ob.name:ob.select_set(True)
bpy.context.view_layer.objects.active=seat_objects[0]
bpy.ops.export_scene.fbx(filepath=str(ROOT/'exports/SM_MKZ_Seating_v2.fbx'),use_selection=True,object_types={'MESH','OTHER'},use_mesh_modifiers=True,bake_anim=False,axis_forward='-Y',axis_up='Z')
report['replaced_stock_seat_faces']=len(remove);report['new_seat_meshes']=len(seat_objects)
