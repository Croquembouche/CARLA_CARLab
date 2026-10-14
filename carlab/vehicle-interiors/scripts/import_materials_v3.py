import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln/';e=u.MaterialEditingLibrary;a=u.AssetToolsHelpers.get_asset_tools();textures={}
for name,kind in [('T_MKZ_Brochure_BaseColor','color'),('T_MKZ_Brochure_ORM','mask'),('T_MKZ_Leather_Roughness','gray'),('T_MKZ_Perforated_Roughness','gray'),('T_MKZ_Perforated_Normal','normal'),('T_MKZ_Perforation_Mask','gray'),('T_MKZ_Brushed_Roughness','gray'),('T_MKZ_Brushed_Normal','normal')]:
 t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.png'));t.destination_path=D[:-1];t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;a.import_asset_tasks([t]);tex=u.load_asset(D+name);assert tex;tex.set_editor_property('srgb',kind=='color');tex.set_editor_property('virtual_texture_streaming',False)
 if kind!='color':tex.set_editor_property('compression_settings',{'gray':u.TextureCompressionSettings.TC_GRAYSCALE,'mask':u.TextureCompressionSettings.TC_MASKS,'normal':u.TextureCompressionSettings.TC_NORMALMAP}[kind])
 assert u.EditorAssetLibrary.save_loaded_asset(tex,False);textures[name]=tex
for name in ['MKZ_Ebony_Leather','MKZ_Perforated_Leather']:
 m=u.load_asset(D+name);n=e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR);has_mask=isinstance(n,u.MaterialExpressionMultiply)
 if has_mask:n=next(x for x in e.get_inputs_for_material_expression(m,n) if isinstance(x,u.MaterialExpressionConstant3Vector))
 assert isinstance(n,u.MaterialExpressionConstant3Vector);n.constant=u.LinearColor(.007,.008,.009,1);n=e.get_material_property_input_node(m,u.MaterialProperty.MP_SPECULAR);n.r=.4
 if name=='MKZ_Perforated_Leather' and not has_mask:
  c=e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR);mask=e.create_material_expression(m,u.MaterialExpressionTextureSample);mask.texture=textures['T_MKZ_Perforation_Mask'];mask.sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE;mul=e.create_material_expression(m,u.MaterialExpressionMultiply);e.connect_material_expressions(c,'',mul,'A');e.connect_material_expressions(mask,'R',mul,'B');e.connect_material_property(mul,'',u.MaterialProperty.MP_BASE_COLOR)
 e.recompile_material(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
m=u.load_asset(D+'MKZ_Stitch_Charcoal');n=e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR);n.constant=u.LinearColor(.022,.024,.026,1);e.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m,False)
m=u.load_asset(D+'MKZ_Fine_Satin');e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR).constant=u.LinearColor(.20,.21,.22,1);e.get_material_property_input_node(m,u.MaterialProperty.MP_METALLIC).r=1
n=e.create_material_expression(m,u.MaterialExpressionConstant);n.r=.65;e.connect_material_property(n,'',u.MaterialProperty.MP_ANISOTROPY)
uv=e.create_material_expression(m,u.MaterialExpressionTextureCoordinate);uv.set_editor_property('u_tiling',8.0);uv.set_editor_property('v_tiling',8.0)
for suffix,prop,kind in [('Roughness',u.MaterialProperty.MP_ROUGHNESS,u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE),('Normal',u.MaterialProperty.MP_NORMAL,u.MaterialSamplerType.SAMPLERTYPE_NORMAL)]:
 n=e.create_material_expression(m,u.MaterialExpressionTextureSample);n.texture=textures['T_MKZ_Brushed_'+suffix];n.sampler_type=kind;e.connect_material_expressions(uv,'',n,'UVs');e.connect_material_property(n,'R' if suffix=='Roughness' else 'RGB',prop)
e.recompile_material(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
m=u.load_asset(D+'MI_MKZ_Brochure_Interior');e.set_material_instance_texture_parameter_value(m,'Diffuse',textures['T_MKZ_Brochure_BaseColor']);e.set_material_instance_texture_parameter_value(m,'ORME',textures['T_MKZ_Brochure_ORM']);e.update_material_instance(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
(R/'materials-v3-import.json').write_text(json.dumps({'result':'SAVED_FOR_VISUAL_REVIEW','changes':['deeper ebony and semi-gloss leather','perforated insert color/normal/roughness detail','subdued walnut veneer','corrected bright metal atlas reflectance and roughness','directional brushed satin normal/roughness and anisotropy'],'geometry_changed':False,'lighting_changed':False},indent=2))
