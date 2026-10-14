import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln/';name='SM_MKZ_Glass_Without_Moonroof';a=u.AssetToolsHelpers.get_asset_tools();opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=False;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=False
opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.fbx'));t.destination_path=D.rstrip('/');t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;t.options=opts;a.import_asset_tasks([t]);mesh=u.load_asset(D+name);assert mesh;n=mesh.get_editor_property('nanite_settings');n.enabled=False;mesh.set_editor_property('nanite_settings',n)
stock=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Meshes/Glass/SM_LincolnMKZ_Glass_Ext1');assert len(mesh.static_materials)==len(stock.static_materials)==2
for i,slot in enumerate(stock.static_materials):mesh.set_material(i,slot.material_interface)
u.EditorAssetLibrary.save_loaded_asset(mesh,False)
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary;changes=[]
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);c=lib.get_object_for_blueprint(data,bp) or lib.get_object(data);name=c.get_name().replace('_GEN_VARIABLE','')
 if name=='SM_Glass_Lincoln_Ext1':c.set_static_mesh(mesh);c.set_editor_property('override_materials',[]);c.set_editor_property('disallow_nanite',True);changes.append(name)
 if name=='MoonroofGlass':c.set_editor_property('override_materials',[]);c.set_translucent_sort_priority(0)
assert changes,changes
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
actor=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector());found={c.get_name():c for c in actor.get_components_by_class(u.StaticMeshComponent)};assert found['SM_Glass_Lincoln_Ext1'].static_mesh==mesh
r={'result':'PASS_SAVED_BLUEPRINT','replaced_components':changes,'glass_bounds':str(mesh.get_bounds()),'material_slots':[str(s.material_interface) for s in mesh.static_materials],'moonroof_material':str(found['MoonroofGlass'].get_material(0))};u.EditorLevelLibrary.destroy_actor(actor);(R/'roof-overlap-import.json').write_text(json.dumps(r,indent=2))
