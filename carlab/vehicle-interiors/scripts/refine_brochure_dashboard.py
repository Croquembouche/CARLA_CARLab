"""Soften coarse dashboard surfaces and model the brochure's closed console finish."""
import bpy,bmesh,math,json,shutil
from pathlib import Path
from collections import defaultdict
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Brochure.blend'));s=bpy.context.scene
assert not bpy.data.collections.get('Brochure_Refinements'),'Already refined; restore the saved before-dashboard source before rebuilding'
backup=R/'backups/before-brochure-dashboard.blend'
if not backup.exists():shutil.copy2(R/'Lincoln_MKZ_Interior_Brochure.blend',backup)
col=bpy.data.collections.new('Brochure_Refinements');s.collection.children.link(col);car=bpy.data.objects['SK_LincolnMKZ.001'];me=car.data;parents=list(range(len(me.vertices)))
def root(x):
 while parents[x]!=x:parents[x]=parents[parents[x]];x=parents[x]
 return x
for edge in me.edges:
 a,b=map(root,edge.vertices);parents[a]=b
groups=defaultdict(list)
for v in me.vertices:groups[root(v.index)].append(v.index)
selected=[]
for ids in groups.values():
 if len(ids)<40:continue
 pts=[car.matrix_world@me.vertices[i].co for i in ids];lo=[min(p[k] for p in pts) for k in range(3)];hi=[max(p[k] for p in pts) for k in range(3)]
 broad=.62<lo[0]<.66 and 1.06<hi[0]<1.09 and .70<lo[2]<.73 and 1.0<hi[2]<1.02 and hi[1]-lo[1]>.7
 hood=.77<lo[0]<.79 and 1.04<hi[0]<1.07 and .92<lo[2]<.94 and 1.0<hi[2]<1.02 and .16<lo[1]<.18 and .56<hi[1]<.59
 if broad or hood:selected.append((ids,lo,hi,'Dashboard' if broad else 'Instrument_Hood'))
assert len(selected)==4,[(len(i),a,b,n) for i,a,b,n in selected]
removed=set();result=[]
for num,(ids,lo,hi,label) in enumerate(selected):
 keep=set(ids);removed.update(keep);bm=bmesh.new();bm.from_mesh(me);bm.verts.ensure_lookup_table()
 bmesh.ops.delete(bm,geom=[f for f in bm.faces if not all(v.index in keep for v in f.verts)],context='FACES')
 bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
 bmesh.ops.join_triangles(bm,faces=list(bm.faces),angle_face_threshold=math.radians(30),angle_shape_threshold=math.radians(100),cmp_uvs=True,cmp_materials=True)
 crease=bm.edges.layers.float.get('crease_edge') or bm.edges.layers.float.new('crease_edge')
 for e in bm.edges:
  if e.is_boundary:e[crease]=1.0 if label=='Dashboard' else .75
 out=bpy.data.meshes.new('Brochure_'+label+str(num));bm.to_mesh(out);bm.free()
 for mat in me.materials:out.materials.append(mat)
 for f in out.polygons:f.use_smooth=True
 obj=bpy.data.objects.new(out.name,out);col.objects.link(obj);obj.matrix_world=car.matrix_world.copy();sub=obj.modifiers.new('Continuous molded surface','SUBSURF');sub.levels=2;sub.render_levels=2;sub.boundary_smooth='PRESERVE_CORNERS'
 result.append({'name':obj.name,'source_vertices':len(ids),'bounds':[lo,hi]})
bm=bmesh.new();bm.from_mesh(me);bmesh.ops.delete(bm,geom=[f for f in bm.faces if all(v.index in removed for v in f.verts)],context='FACES');bm.to_mesh(me);bm.free()
def material(name,color,rough,metal):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;return m
satin=material('MKZ_Brochure_Satin',(.35,.37,.39),.30,.90);chrome=material('MKZ_Brochure_Chrome',(.52,.55,.58),.22,1);leather=bpy.data.materials['MKZ_Ebony_Leather']
def box(name,loc,dims,mat,bevel):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name='Brochure_'+name;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 for c in list(o.users_collection):c.objects.unlink(o)
 col.objects.link(o);o.data.materials.append(mat);be=o.modifiers.new('Manufactured edge radius','BEVEL');be.width=bevel;be.segments=6
 for f in o.data.polygons:f.use_smooth=True
 o.modifiers.new('Corner normal weighting','WEIGHTED_NORMAL');return o
box('Console_Sliding_Cover',(.328,0,.701),(.336,.215,.009),satin,.007)
box('Console_Cover_Finger_Pull',(.191,0,.711),(.009,.061,.008),chrome,.003)
# Retain stock armrest envelope and add its gently padded leather crown.
box('Center_Armrest_Leather',(-.055,0,.725),(.342,.227,.039),leather,.018)
# Fine edging alongside the sliding panel keeps metal and leather visually distinct.
for sign in [-1,1]:box('Console_Edge_'+str(sign),(.335,sign*.113,.702),(.355,.004,.004),chrome,.0017)
# The stock cloth sunshade trim and the already completed roof remain unchanged.
bpy.ops.file.pack_all()
for name in ['Completed','Final','Brochure']:bpy.ops.wm.save_as_mainfile(filepath=str(R/f'Lincoln_MKZ_Interior_{name}.blend'))
(R/'brochure-dashboard-build.json').write_text(json.dumps({'result':'SAVED_VISUAL_REVIEW_PENDING','smoothed_surfaces':result,'new_trim':[o.name for o in col.objects]},indent=2))
exec(compile((R/'scripts/export_completed_cabin.py').read_text(),'export_completed_cabin.py','exec'))
shutil.copy2(R/'exports/SM_MKZ_Cabin_Final.fbx',R/'exports/SM_MKZ_Cabin_Brochure.fbx')
