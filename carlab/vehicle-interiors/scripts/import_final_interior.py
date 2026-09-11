import unreal as u,json,os
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';A=u.AssetToolsHelpers.get_asset_tools()
stock=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/SK_LincolnMKZ')
def task(name,skeletal):
 existing=u.load_asset(D+('/SK_MKZ_Exterior_Final' if skeletal else '/'+name))
 if existing and os.environ.get('LINCOLN_REIMPORT')!='1':return existing
 opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=skeletal;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else u.FBXImportType.FBXIT_STATIC_MESH
 if skeletal:
  opts.create_physics_asset=False;opts.import_animations=False;opts.skeleton=stock.skeleton;opts.physics_asset=stock.physics_asset
  opts.skeletal_mesh_import_data.set_editor_property('update_skeleton_reference_pose',False)
  opts.skeletal_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
 else:
  opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=False
  opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
 asset_name='SK_MKZ_Exterior_Final' if skeletal else name
 t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.fbx'));t.destination_path=D;t.destination_name=asset_name;t.automated=True;t.replace_existing=True;t.save=True;t.options=opts;A.import_asset_tasks([t]);asset=u.load_asset(D+'/'+asset_name);assert asset;return asset
exterior=task('SK_MKZ_ExteriorOnly',True)
c=u.SkeletalMeshComponent();c.set_skeletal_mesh_asset(exterior);bones=[str(c.get_bone_name(i)) for i in range(c.get_num_bones())]
expected=json.loads((R/'vehicle-rig-original.json').read_text())['bones'];assert bones==expected,(bones,expected)
assert exterior.skeleton==stock.skeleton
original_component=u.SkeletalMeshComponent();original_component.set_skeletal_mesh_asset(stock)
pose_report={}
for i,name in enumerate(bones):
 original_pose=original_component.get_ref_pose_transform(i);pose=c.get_ref_pose_transform(i)
 pose_report[name]={'stock':str(original_pose),'final':str(pose)}
(R/'final-rig-compatibility.json').write_text(json.dumps(pose_report,indent=2))
for i,name in enumerate(bones):
 original_pose=original_component.get_ref_pose_transform(i);pose=c.get_ref_pose_transform(i)
 assert (pose.translation-original_pose.translation).length()<.01,(name,'position',str(pose))
 assert (pose.scale3d-original_pose.scale3d).length()<.001,(name,'scale',str(pose))
 q=pose.rotation;p=original_pose.rotation
 assert abs(q.x*p.x+q.y*p.y+q.z*p.z+q.w*p.w)>.99999,(name,'rotation',str(pose))
lods=exterior.get_editor_property('lod_info')
for lod in lods:lod.set_editor_property('allow_cpu_access',True)
exterior.set_editor_property('lod_info',lods)
exterior.set_editor_property('physics_asset',stock.physics_asset)
assert exterior.physics_asset==stock.physics_asset
exterior.set_editor_property('materials',stock.get_editor_property('materials'));assert u.EditorAssetLibrary.save_loaded_asset(exterior,False)
cabin=task('SM_MKZ_Cabin_Final',False)
nanite=cabin.get_editor_property('nanite_settings');nanite.enabled=False;cabin.set_editor_property('nanite_settings',nanite)
for i,slot in enumerate(cabin.static_materials):
 name=str(slot.material_slot_name)
 path='MI_MKZ_Final_Interior' if 'MI_LincolnMKZ_Interior01' in name else ('M_Interior_Mat' if name=='MKZ_Seatbelt_Webbing' else name)
 material=u.load_asset(D+'/'+path);assert material,(name,path);cabin.set_material(i,material)
u.EditorAssetLibrary.save_loaded_asset(cabin,False)
bp=u.load_asset(D+'/BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
changes=[]
for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(handle);obj=lib.get_object(data);name=obj.get_name().replace('_GEN_VARIABLE','')
 if name=='VehicleMesh':
  obj=lib.get_object_for_blueprint(data,bp) or obj
  obj.set_skeletal_mesh_asset(exterior);obj.set_editor_property('override_materials',[slot.material_interface for slot in exterior.materials]);changes.append(name)
 elif name.startswith('SM_LincolnDoor_'):
  obj.set_material(2,u.load_asset(D+'/MI_MKZ_Final_Interior'))
 elif name=='CabinReplacement':
  obj.set_static_mesh(cabin);obj.set_editor_property('override_materials',[]);obj.set_editor_property('disallow_nanite',True);obj.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);changes.append(name)
handles=sub.k2_gather_subobject_data_for_blueprint(bp)
for h in handles:
 obj=lib.get_object(lib.get_data(h))
 name=obj.get_name().replace('_GEN_VARIABLE','')
 if name=='MannequinPreview':sub.delete_subobject(handles[0],h,bp)
 elif name=='CustomCollision':
  obj=lib.get_object_for_blueprint(lib.get_data(h),bp) or obj
  obj.set_visibility(False);obj.set_hidden_in_game(True);obj.set_cast_shadow(False)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
actor=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector())
meshes={o.get_name():o for o in actor.get_components_by_class(u.MeshComponent)}
assert meshes['VehicleMesh'].skeletal_mesh_asset==exterior
assert meshes['CabinReplacement'].static_mesh==cabin
report={'result':'PASS_IMPORT','bones':bones,'exterior':exterior.get_path_name(),'cabin':cabin.get_path_name(),'exterior_bounds':str(exterior.get_bounds()),'cabin_bounds':str(cabin.get_bounds()),'changes':changes,'cabin_materials':[str(meshes['CabinReplacement'].get_material(i).get_path_name()) for i in range(meshes['CabinReplacement'].get_num_materials())]}
u.EditorLevelLibrary.destroy_actor(actor);(R/'final-unreal-import.json').write_text(json.dumps(report,indent=2))
