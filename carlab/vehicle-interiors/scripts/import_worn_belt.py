import unreal as u
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';name='SM_MKZ_Driver_Worn_Belt'
opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=False;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=False;opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.fbx'));t.destination_path=D;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;t.options=opts;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t]);m=u.load_asset(D+'/'+name);assert m
for i,s in enumerate(m.static_materials):m.set_material(i,u.load_asset(D+'/MKZ_Seatbelt_Webbing'))
assert u.EditorAssetLibrary.save_loaded_asset(m,False)
