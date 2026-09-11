import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';stock=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/SK_LincolnMKZ');mesh=u.load_asset(D+'SK_MKZ_Exterior_Final')
mesh.set_editor_property('materials',stock.get_editor_property('materials'));u.EditorAssetLibrary.save_loaded_asset(mesh,False)
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);obj=lib.get_object(data)
 if obj.get_name()=='VehicleMesh':
  obj=lib.get_object_for_blueprint(data,bp) or obj;obj.set_editor_property('override_materials',[x.material_interface for x in stock.materials])
u.BlueprintEditorLibrary.compile_blueprint(bp);u.EditorAssetLibrary.save_loaded_asset(bp,False)
