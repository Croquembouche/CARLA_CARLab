import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors/reference/glazing');R.mkdir(exist_ok=True)
D='/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Meshes/Glass/';r={}
for name in ['SM_LincolnMKZ_Glass_Ext1','SM_LincolnMKZ_Glass_Ext2','SM_LincolnMKZ_Door_FL_Glass_Ext1','SM_LincolnMKZ_Door_FR_Glass_Ext1','SM_LincolnMKZ_Door_RL_Glass_Ext1','SM_LincolnMKZ_Door_RR_Glass_Ext1']:
 m=u.load_asset(D+name);t=u.AssetExportTask();t.object=m;t.filename=str(R/(name+'.fbx'));t.automated=True;t.prompt=False;t.replace_identical=True;t.options=u.FbxExportOption();t.options.ascii=False;assert u.Exporter.run_asset_export_task(t);r[name]={'bounds':str(m.get_bounds()),'materials':[str(x.material_interface) for x in m.static_materials]}
for suffix in ['Int','Ext']:
 m=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Materials/MI_LincolnMKZ_Glass_'+suffix)
 r['material_'+suffix]={'parent':str(m.parent),'scalar':{str(n):u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(m,n) for n in u.MaterialEditingLibrary.get_scalar_parameter_names(m)},'vector':{str(n):str(u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(m,n)) for n in u.MaterialEditingLibrary.get_vector_parameter_names(m)}}
(R/'audit.json').write_text(json.dumps(r,indent=2))
