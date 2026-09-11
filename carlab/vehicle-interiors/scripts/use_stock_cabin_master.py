import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';lib=u.MaterialEditingLibrary
source=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Materials/MI_LincolnMKZ_Interior01')
m=u.load_asset(D+'MI_MKZ_Final_Interior') or u.EditorAssetLibrary.duplicate_loaded_asset(source,D+'MI_MKZ_Final_Interior')
names=[str(x) for x in lib.get_texture_parameter_names(m)];r={'texture_parameters':names};assert 'Diffuse' in names and 'ORME' in names,names
for param,tex in [('Diffuse','T_MKZ_Ebony_BaseColor'),('ORME','T_MKZ_Ebony_ORM')]:
 texture=u.load_asset(D+tex);lib.set_material_instance_texture_parameter_value(m,param,texture)
 assert lib.get_material_instance_texture_parameter_value(m,param)==texture
lib.set_material_instance_scalar_parameter_value(m,'Normal Intensity',.09);lib.update_material_instance(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
cabin=u.load_asset(D+'SM_MKZ_Cabin_Final');cabin.set_material(0,m);u.EditorAssetLibrary.save_loaded_asset(cabin,False)
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);f=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=f.get_data(h);o=f.get_object_for_blueprint(data,bp) or f.get_object(data);name=o.get_name().replace('_GEN_VARIABLE','')
 if name=='CabinReplacement':o.set_material(0,m)
 elif name.startswith('SM_LincolnDoor_'):o.set_material(2,m)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
open('/mnt/simulations/vehicle-interiors/stock-master-material-fix.json','w').write(json.dumps(r,indent=2))
