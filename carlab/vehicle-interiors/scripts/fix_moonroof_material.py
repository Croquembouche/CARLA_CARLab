import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';source=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Materials/MI_LincolnMKZ_Glass_Ext');m=u.load_asset(D+'MI_MKZ_Moonroof_Tint') or u.EditorAssetLibrary.duplicate_loaded_asset(source,D+'MI_MKZ_Moonroof_Tint')
parent=u.load_asset(D+'M_MKZ_Moonroof_Master') or u.EditorAssetLibrary.duplicate_loaded_asset(source.parent,D+'M_MKZ_Moonroof_Master')
parent.set_editor_property('translucency_pass',u.MaterialTranslucencyPass.MTP_BEFORE_DOF);parent.set_editor_property('two_sided',True);u.MaterialEditingLibrary.recompile_material(parent);u.EditorAssetLibrary.save_loaded_asset(parent,False);m.set_editor_property('parent',parent)
p=m.get_editor_property('base_property_overrides');p.set_editor_property('override_two_sided',True);p.set_editor_property('two_sided',True);m.set_editor_property('base_property_overrides',p)
lib=u.MaterialEditingLibrary;lib.set_material_instance_scalar_parameter_value(m,'Opacity',.60);lib.set_material_instance_scalar_parameter_value(m,'Roughness',.08);lib.set_material_instance_vector_parameter_value(m,'Color',u.LinearColor(.025,.045,.055,1));lib.update_material_instance(m);u.EditorAssetLibrary.save_loaded_asset(m,False)
mesh=u.load_asset(D+'SM_MKZ_Moonroof_Glass');mesh.set_material(0,m);u.EditorAssetLibrary.save_loaded_asset(mesh,False)
open('/mnt/simulations/vehicle-interiors/moonroof-material.json','w').write(json.dumps({'material':m.get_path_name(),'opacity':lib.get_material_instance_scalar_parameter_value(m,'Opacity'),'two_sided':str(p)},indent=2))
