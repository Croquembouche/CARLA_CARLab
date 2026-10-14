"""Rebuild upholstery from the saved completed assembly; preserve all cabin fittings."""
import bpy,math,json,shutil
from pathlib import Path
from mathutils import Vector
ROOT=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_Completed.blend'));scene=bpy.context.scene
backup=ROOT/'backups/before-brochure-seating.blend'
if not backup.exists():shutil.copy2(ROOT/'Lincoln_MKZ_Interior_Completed.blend',backup)
for o in list(scene.objects):
 if o.name.startswith(('Front_Left_','Front_Right_','Rear_Left_','Rear_Right_','Rear_Center_')) and 'Interior_Completion' not in [c.name for c in o.users_collection]:bpy.data.objects.remove(o,do_unlink=True)
def mat(name,color,rough=.55,metal=0,grain=0):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1);nt=m.node_tree;nt.nodes.clear();out=nt.nodes.new('ShaderNodeOutputMaterial');p=nt.nodes.new('ShaderNodeBsdfPrincipled');nt.links.new(p.outputs[0],out.inputs[0]);p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;p.inputs['Specular IOR Level'].default_value=.28
 if grain:
  co=nt.nodes.new('ShaderNodeTexCoord');n=nt.nodes.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=1300;n.inputs['Detail'].default_value=2;nt.links.new(co.outputs['Object'],n.inputs['Vector']);b=nt.nodes.new('ShaderNodeBump');b.inputs['Strength'].default_value=.12;b.inputs['Distance'].default_value=grain;nt.links.new(n.outputs['Fac'],b.inputs['Height']);nt.links.new(b.outputs[0],p.inputs['Normal'])
 return m
leather=mat('MKZ_Ebony_Leather',(.018,.021,.024),.55,grain=.00008);thread=mat('MKZ_Stitch_Charcoal',(.037,.04,.043),.8)
def line(name,points,radius=.00035):
 c=bpy.data.curves.new(name,'CURVE');c.dimensions='3D';c.bevel_depth=radius;c.bevel_resolution=2;sp=c.splines.new('POLY');sp.points.add(len(points)-1)
 for p,co in zip(sp.points,points):p.co=(*co,1)
 o=bpy.data.objects.new(name,c);scene.collection.objects.link(o);c.materials.append(thread)
report={}
source=(ROOT/'scripts/seat_upgrade.py').read_text();source=source[source.index('seat_objects=[]'):source.index('# Export the new seat module')]
source=source.replace("(.016,.019,.022),.48", "(.016,.019,.022),.60")
source=source.replace('nu=36,nv=36','nu=64,nv=64')
source=source.replace('.072*math.exp(-((abs(2*u-1)-.80)/.22)**2)*math.sin(math.pi*v)**.35+.018*math.exp(-((v-.91)/.09)**2)', '.042*math.exp(-((abs(2*u-1)-.82)/.27)**2)*math.sin(math.pi*v)**.6+.009*math.exp(-((v-.87)/.14)**2)-.003*math.exp(-((u-.5)/.015)**2)')
source=source.replace('vv=.10+.79*v;uu=.19+.62*u','vv=.09+.68*v;uu=.20+.60*u')
source=source.replace('x+=.003+.0015*math.cos(v*math.pi*6)','x+=.0012')
source=source.replace('.045*math.exp(-((abs(2*u-1)-.80)/.22)**2)*math.sin(math.pi*v)**.35','.032*math.exp(-((abs(2*u-1)-.84)/.27)**2)*math.sin(math.pi*v)**.6')
# Replace the headrest primitive with a smooth upholstered superellipsoid, retaining socket locations.
start=source.index('def rounded_box(');end=source.index('\ndef pad(',start)
old=source[start:end]
new=old.replace(" bpy.ops.mesh.primitive_cube_add", " if 'Headrest' in name:return upholstered_headrest(name,loc,size,material)\n bpy.ops.mesh.primitive_cube_add",1)
source=source[:start]+new+source[end:]
def upholstered_headrest(name,loc,size,material):
 nu,nv=80,40;verts=[]
 def sp(x,e):return math.copysign(abs(x)**e,x)
 for j in range(nv+1):
  lat=-math.pi/2+math.pi*j/nv
  for i in range(nu):
   lon=2*math.pi*i/nu;cx=sp(math.cos(lat),.55);z=sp(math.sin(lat),.55)*size[2]/2
   # Convex face, rolled corners and subtle forward tilt, rather than flat cube faces.
   x=sp(math.cos(lon),.65)*cx*size[0]/2 + .055*z
   y=sp(math.sin(lon),.65)*cx*size[1]/2*(1-.055*z/(size[2]/2))
   verts.append((x+loc[0],y+loc[1],z+loc[2]))
 faces=[]
 for j in range(nv):
  for i in range(nu):
   a=j*nu+i;b=j*nu+(i+1)%nu;faces.append((a,b,b+nu,a+nu))
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.materials.append(material);me.update();o=bpy.data.objects.new(name,me);scene.collection.objects.link(o);seat_objects.append(o)
 uv=me.uv_layers.new(name='UpholsteryUV')
 for f in me.polygons:
  f.use_smooth=True
  for li in f.loop_indices:
   vi=me.loops[li].vertex_index;uv.data[li].uv=(vi%nu/nu,vi//nu/nv)
 return o
exec(compile(source,'brochure-seat-builder','exec'))
# Rear center upholstery follows the same rake as the outboard seats.
for name in ['Rear_Center_Backrest','Rear_Center_Cushion']:
 o=bpy.data.objects.get(name);seat_objects.remove(o);bpy.data.objects.remove(o,do_unlink=True)
def rearback(u,v):return(-.98-.235*v+.018*math.sin(math.pi*v), (u*2-1)*.145, .63+.47*v)
def rearbase(u,v):return(-1.08+.53*v,(u*2-1)*.145,.575+.018*math.sin(math.pi*v))
mesh_grid('Rear_Center_Backrest',rearback,material=leather,thickness=.07);mesh_grid('Rear_Center_Cushion',rearbase,material=leather,thickness=.065)
# One horizontal seam above each perforated panel, as in brochure page 4.
for prefix in ['Front_Left','Front_Right','Rear_Left','Rear_Right']:
 panel=bpy.data.objects[prefix+'_Perforated_Backrest'];n=65
 points=[tuple(panel.data.vertices[(n-1)*n+i].co+Vector((.002,0,0))) for i in range(n)]
 for i in range(0,len(points)-1,2):line(prefix+'_Shoulder_Stitch',points[i:i+2])
# Remove extra historical buckle blocks: current complete housings are retained.
for o in list(scene.objects):
 if o.name.endswith(('_Seatbelt_Buckle','_Red_Release')) and 'Interior_Completion' not in [c.name for c in o.users_collection]:bpy.data.objects.remove(o,do_unlink=True)
assert all(math.isfinite(c) for o in scene.objects if o.type=='MESH' for v in o.data.vertices for c in v.co)
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_Completed.blend'));bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_Final.blend'));bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_Brochure.blend'))
(ROOT/'brochure-seating-build.json').write_text(json.dumps({'result':'GEOMETRY_SAVED_VISUAL_REVIEW_PENDING','reference':'2020 MKZ brochure pages 3-4, ebony interior','changes':['convex headrests','softer bolsters','plain upper shoulder panels','reclined rear center seat','subdued leather and thread'],'roof_and_belts_preserved':True},indent=2))
exec(compile((ROOT/'scripts/export_completed_cabin.py').read_text(),'export_completed_cabin.py','exec'))
