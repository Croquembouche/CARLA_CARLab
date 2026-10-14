import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';a=u.AssetToolsHelpers.get_asset_tools();e=u.MaterialEditingLibrary
# Reuse the verified simple-material authoring helper, without importing or changing the vehicle.
source=(R/'scripts/import_completion.py').read_text();exec(compile(source[:source.index('meshes={}')],'material-helpers','exec'))
material('MKZ_Brochure_Satin',(.35,.37,.39),.30,.90);material('MKZ_Brochure_Chrome',(.52,.55,.58),.22,1)
textures={}
for suffix in ['BaseColor','ORM']:
 name='T_MKZ_Brochure_'+suffix;t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.png'));t.destination_path=D;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;a.import_asset_tasks([t]);tex=u.load_asset(D+'/'+name);assert tex;tex.set_editor_property('srgb',suffix=='BaseColor');tex.set_editor_property('virtual_texture_streaming',False)
 if suffix=='ORM':tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_MASKS)
 assert u.EditorAssetLibrary.save_loaded_asset(tex,False);textures[suffix]=tex
m=u.load_asset(D+'/MI_MKZ_Brochure_Interior') or u.EditorAssetLibrary.duplicate_loaded_asset(u.load_asset(D+'/MI_MKZ_Final_Interior'),D+'/MI_MKZ_Brochure_Interior')
for param,suffix in [('Diffuse','BaseColor'),('ORME','ORM')]:e.set_material_instance_texture_parameter_value(m,param,textures[suffix])
e.update_material_instance(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
name='SM_MKZ_Cabin_Brochure';opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=False;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=False;opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
request=u.AssetImportTask();request.filename=str(R/'exports'/(name+'.fbx'));request.destination_path=D;request.destination_name=name;request.automated=True;request.replace_existing=True;request.save=True;request.options=opts;a.import_asset_tasks([request]);mesh=u.load_asset(D+'/'+name);assert mesh
ns=mesh.get_editor_property('nanite_settings');ns.enabled=False;mesh.set_editor_property('nanite_settings',ns)
for i,slot in enumerate(mesh.static_materials):
 label=str(slot.material_slot_name);mat=m if 'MI_LincolnMKZ_Interior01' in label else u.load_asset(D+'/'+label);assert mat,label;mesh.set_material(i,mat)
assert u.EditorAssetLibrary.save_loaded_asset(mesh,False)
(R/'brochure-cabin-import.json').write_text(json.dumps({'result':'IMPORTED_FOR_VISUAL_REVIEW','mesh':mesh.get_path_name(),'material':m.get_path_name(),'materials':[str(x.material_interface) for x in mesh.static_materials]},indent=2))
