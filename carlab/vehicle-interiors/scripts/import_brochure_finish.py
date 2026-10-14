import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';a=u.AssetToolsHelpers.get_asset_tools();e=u.MaterialEditingLibrary
# Keep authored normals at the rounded upholstery edges when importing FBX.
source=(R/'scripts/import_completion.py').read_text().replace('opts.static_mesh_import_data.combine_meshes=True;', 'opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;opts.static_mesh_import_data.combine_meshes=True;')
exec(compile(source,'import_completion.py','exec'))
for kind,material,color,rough in [('Leather','MKZ_Ebony_Leather',(.018,.021,.024),.55),('Perforated','MKZ_Perforated_Leather',(.016,.019,.022),.60)]:
 m=u.load_asset(D+'/'+material)
 for suffix,prop in [('Normal',u.MaterialProperty.MP_NORMAL),('Roughness',u.MaterialProperty.MP_ROUGHNESS)]:
  name=f'T_MKZ_{kind}_{suffix}';t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.png'));t.destination_path=D;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;a.import_asset_tasks([t]);tex=u.load_asset(D+'/'+name);assert tex
  tex.set_editor_property('srgb',False);tex.set_editor_property('virtual_texture_streaming',False)
  tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if suffix=='Normal' else u.TextureCompressionSettings.TC_GRAYSCALE);u.EditorAssetLibrary.save_loaded_asset(tex,False)
  node=e.create_material_expression(m,u.MaterialExpressionTextureSample);node.texture=tex;node.sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL if suffix=='Normal' else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE;e.connect_material_property(node,'RGB' if suffix=='Normal' else 'R',prop)
 n=e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR);n.constant=u.LinearColor(*color,1)
 n=e.create_material_expression(m,u.MaterialExpressionConstant);n.r=.28;e.connect_material_property(n,'',u.MaterialProperty.MP_SPECULAR)
 e.recompile_material(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
m=u.load_asset(D+'/MKZ_Stitch_Charcoal');n=e.get_material_property_input_node(m,u.MaterialProperty.MP_BASE_COLOR);n.constant=u.LinearColor(.037,.04,.043,1);e.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m,False)
(R/'brochure-finish-import.json').write_text(json.dumps({'result':'IMPORTED','normals':'authored FBX normals','leather_detail':'normal and roughness textures, nonmetallic, specular .28','visual_review':'PENDING'},indent=2))
