import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';r={};mesh=u.load_asset(D+'SK_MKZ_Exterior_Final')
lods=mesh.get_editor_property('lod_info');r['lod_cpu_before']=[x.get_editor_property('allow_cpu_access') for x in lods]
for x in lods:x.set_editor_property('allow_cpu_access',True)
mesh.set_editor_property('lod_info',lods);u.EditorAssetLibrary.save_loaded_asset(mesh,False)
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);obj=lib.get_object_for_blueprint(data,bp) or lib.get_object(data)
 if isinstance(obj,u.MeshComponent) and obj.get_name().replace('_GEN_VARIABLE','')=='CustomCollision':
  r['collision_before']={n:str(obj.get_editor_property(n)) for n in ['visible','hidden_in_game','render_in_main_pass','cast_shadow']}
  obj.set_visibility(False);obj.set_hidden_in_game(True);obj.set_cast_shadow(False)
u.BlueprintEditorLibrary.compile_blueprint(bp);u.EditorAssetLibrary.save_loaded_asset(bp,False)
m=u.load_asset(D+'M_MKZ_Interior_PBR');u.MaterialEditingLibrary.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m,False)
open('/mnt/simulations/vehicle-interiors/runtime-detail-fix.json','w').write(json.dumps(r,indent=2))
