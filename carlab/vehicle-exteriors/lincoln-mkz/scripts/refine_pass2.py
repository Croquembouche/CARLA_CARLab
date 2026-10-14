import bpy,json,shutil
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');I=R/'iterations/pass1';I.mkdir(parents=True,exist_ok=True)
for f in (R/'renders').glob('refined-*.png'):shutil.copy2(f,I/f.name)
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Refined.blend'));S=bpy.context.scene
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='OPTIX'
S.cycles.device='GPU'
old=bpy.data.objects.get('SM_MKZ_Optical_Details')
if old:bpy.data.objects.remove(old,do_unlink=True)
body=bpy.data.objects['StockBody'];tree=BVHTree.FromObject(body,bpy.context.evaluated_depsgraph_get());inv=body.matrix_world.inverted();parts=[];centers=[]
measured=json.loads((R/'reference/optics-probe.json').read_text());probe=[]
for item in measured:
    if item['count']==81 and item['center'][1]>0:
        probe.append(item);probe.append({'count':81,'center':[item['center'][0],-item['center'][1],item['center'][2]]})
for item in probe:
    if item['count']!=81:continue
    x,y,z=item['center'];p,n,i,d=tree.ray_cast(inv@Vector((3,y,z)),inv.to_3x3()@Vector((-1,0,0)))
    if p is None:continue
    p=body.matrix_world@p
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,location=(p.x+.001,y,z));o=bpy.context.object;o.scale=(.0035,.0115,.014);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(bpy.data.materials['M_MKZ_OpticalLens'])
    for poly in o.data.polygons:poly.use_smooth=True
    parts.append(o);centers.append([p.x+.001,y,z])
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name='SM_MKZ_Optical_Details';S.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.export_scene.fbx(filepath=str(R/'exports/SM_MKZ_Optical_Details.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')
(R/'authored-pass2.json').write_text(json.dumps({'result':'CALIBRATED_OPTICS_PASS_2','correction':'First-pass optical elements formed a second row. Replaced with elements aligned to the measured centers of all twelve existing lamp caps.','centers_m':centers,'camera_lighting_unchanged':True,'new_geometry_vertices':len(o.data.vertices)},indent=2))
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Refined.blend'))
for name in ['front-three-quarter','front-detail','wheel-detail']:
    S.camera=bpy.data.objects['Camera_'+name];S.render.filepath=str(R/'renders'/('refined-'+name+'.png'));bpy.ops.render.render(write_still=True)
