import bpy
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));s=bpy.context.scene
body=bpy.data.objects['SK_SeatedMannequin'];ev=body.evaluated_get(bpy.context.evaluated_depsgraph_get());inv=ev.matrix_world.inverted();o=bpy.data.objects['Front_Left_Worn_Shoulder_Belt'];pts=[Vector(v) for v in [(-.24,.625,1.25),(-.08,.57,1.18),(.275,.515,1.15),(.36,.41,1.06),(.39,.29,.93),(.35,.19,.78),(.185,.14,.66)]];new=[]
for a,b in zip(pts,pts[1:]):
 for j in range(10):
  p=a.lerp(b,j/10);origin=inv@Vector((1.2,p.y,p.z));direction=(inv.to_3x3()@Vector((-1,0,0))).normalized();hit,loc,normal,index=ev.ray_cast(origin,direction)
  if hit:
   v=ev.matrix_world@loc
   if v.x>-.05:p.x=v.x+.009
  new.append(p)
new.append(pts[-1]);verts=[]
for i,p in enumerate(new):
 t=new[min(i+1,len(new)-1)]-new[max(0,i-1)];w=Vector((1,0,0)).cross(t).normalized()*.0235;verts += [p-w,p+w]
me=bpy.data.meshes.new('Body fitted shoulder webbing');me.from_pydata(verts,[],[(i*2,i*2+1,i*2+3,i*2+2) for i in range(len(new)-1)]);me.materials.append(bpy.data.materials['MKZ_Seatbelt_Webbing']);o.data=me

# Remove obsolete buckle blocks; the completion collection supplies full housings/stalks.
for prefix in ['Front_Left','Front_Right','Rear_Left','Rear_Right']:
 for suffix in ['_Seatbelt_Buckle','_Red_Release']:
  old=bpy.data.objects.get(prefix+suffix)
  if old and 'Interior_Completion' not in [c.name for c in old.users_collection]:bpy.data.objects.remove(old,do_unlink=True)
col=bpy.data.collections['Interior_Completion']
def part(name,loc,dims,material):
 old=bpy.data.objects.get(name)
 if old:bpy.data.objects.remove(old,do_unlink=True)
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);obj=bpy.context.object;obj.name=name;obj.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 for c in list(obj.users_collection):c.objects.unlink(obj)
 col.objects.link(obj);obj.data.materials.append(bpy.data.materials[material]);bevel=obj.modifiers.new('Rounded edges','BEVEL');bevel.width=.004;bevel.segments=3;obj.modifiers.new('Normals','WEIGHTED_NORMAL')
part('Rear_Center_Seatbelt_Buckle',(-.91,-.145,.626),(.065,.04,.065),'MKZ_Seat_Base_Plastic')
part('Rear_Center_Buckle_Release',(-.875,-.145,.642),(.008,.028,.028),'MKZ_Belt_Release')
part('Front_Left_Lap_Anchor',(.22,.625,.57),(.06,.025,.05),'MKZ_Seat_Hardware')
lap=bpy.data.objects['Front_Left_Worn_Lap_Belt'];shift=Vector((.185,.14,.66))-(lap.data.vertices[0].co+lap.data.vertices[1].co)/2
for v in list(lap.data.vertices)[:2]:v.co+=shift

bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
col=bpy.data.collections['Interior_Completion'];bpy.ops.object.select_all(action='DESELECT')
for obj in col.objects:obj.select_set(True)
bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH');bpy.ops.export_scene.fbx(filepath=str(R/'exports/SM_MKZ_Completion_Details.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')

exec(compile((R/'scripts/export_completed_cabin.py').read_text(),'export_completed_cabin.py','exec'))

# Keep worn webbing separate for empty-cabin reference captures.
exec(compile((R/'scripts/export_brochure_belts.py').read_text(),'export_brochure_belts.py','exec'))
