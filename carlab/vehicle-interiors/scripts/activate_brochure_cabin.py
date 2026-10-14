import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln/';a=u.AssetToolsHelpers.get_asset_tools();e=u.MaterialEditingLibrary
for kind,name,color in [('Leather','MKZ_Ebony_Leather',(.013,.015,.017)),('Perforated','MKZ_Perforated_Leather',(.012,.014,.016))]:
 texname=f'T_MKZ_{kind}_Roughness';t=u.AssetImportTask();t.filename=str(R/'exports'/(texname+'.png'));t.destination_path=D[:-1];t.destination_name=texname;t.automated=True;t.replace_existing=True;t.save=True;a.import_asset_tasks([t]);tex=u.load_asset(D+texname);tex.set_editor_property('srgb',False);tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_GRAYSCALE);tex.set_editor_property('virtual_texture_streaming',False);u.EditorAssetLibrary.save_loaded_asset(tex,False)
 mat=u.load_asset(D+name);n=e.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR);n.constant=u.LinearColor(*color,1);n=e.get_material_property_input_node(mat,u.MaterialProperty.MP_SPECULAR);n.r=.35;e.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat,False)
mat=u.load_asset(D+'MKZ_Brochure_Satin');n=e.get_material_property_input_node(mat,u.MaterialProperty.MP_ROUGHNESS);n.r=.4;e.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat,False)
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');mesh=u.load_asset(D+'SM_MKZ_Cabin_Brochure');interior=u.load_asset(D+'MI_MKZ_Brochure_Interior');assert bp and mesh and interior
sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary;changed=[]
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp) or lib.get_object(data);name=o.get_name().replace('_GEN_VARIABLE','')
 if name=='CabinReplacement':o.set_static_mesh(mesh);o.set_editor_property('override_materials',[]);changed.append(name)
 elif name.startswith('SM_LincolnDoor_'):o.set_material(2,interior);changed.append(name)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
(R/'brochure-activation.json').write_text(json.dumps({'result':'SAVED_AND_COMPILED','blueprint':bp.get_path_name(),'cabin':mesh.get_path_name(),'changed':changed,'visual_acceptance':'PENDING_FINAL_FRESH_RUN'},indent=2))
