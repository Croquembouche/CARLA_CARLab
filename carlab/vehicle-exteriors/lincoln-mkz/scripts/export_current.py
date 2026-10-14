import unreal as u
import json, hashlib
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz')
B='/Game/Carla/Static/Car/4Wheeled/LincolnMKZ'
report={'stock_blueprint':'/Game/Carla/Blueprints/Vehicles/LincolnMKZ/BP_LincolnMKZ','exports':[],'materials':{},'components':[]}
for path in u.EditorAssetLibrary.list_assets(B,True,False):
    a=u.load_asset(path)
    if isinstance(a,(u.SkeletalMesh,u.StaticMesh,u.Texture2D)) and '/Parked/' not in path:
        ext='.tga' if isinstance(a,u.Texture2D) else '.fbx'
        t=u.AssetExportTask();t.object=a;t.filename=str(R/'reference'/(a.get_name()+ext));t.automated=True;t.prompt=False;t.replace_identical=True
        if ext=='.fbx':
            t.options=u.FbxExportOption();t.options.ascii=False;t.options.level_of_detail=False;t.options.bake_material_inputs=u.FbxMaterialBakeMode.DISABLED
        if not Path(t.filename).exists():assert u.Exporter.run_asset_export_task(t),path
        e={'asset':path,'file':t.filename,'sha256':hashlib.sha256(Path(t.filename).read_bytes()).hexdigest()}
        if isinstance(a,u.SkeletalMesh):e['materials']=[str(x.material_interface.get_path_name()) for x in a.materials]
        elif isinstance(a,u.StaticMesh):e['materials']=[str(x.material_interface.get_path_name()) for x in a.static_materials]
        report['exports'].append(e)
    if isinstance(a,u.MaterialInstanceConstant):
        l=u.MaterialEditingLibrary
        report['materials'][a.get_name()]={'path':path,'parent':a.parent.get_path_name(),'scalar':{str(n):l.get_material_instance_scalar_parameter_value(a,n)for n in l.get_scalar_parameter_names(a)},'vector':{str(n):str(l.get_material_instance_vector_parameter_value(a,n))for n in l.get_vector_parameter_names(a)},'texture':{str(n):str(l.get_material_instance_texture_parameter_value(a,n))for n in l.get_texture_parameter_names(a)}}
bp=u.load_asset(report['stock_blueprint']);sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
    data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp) or lib.get_object(data)
    e={'name':o.get_name(),'class':o.get_class().get_name()}
    for k in ['relative_location','relative_rotation','relative_scale3d','static_mesh','skeletal_mesh','override_materials']:
        try:e[k]=str(o.get_editor_property(k))
        except Exception:pass
    report['components'].append(e)
(R/'reference/current-assets.json').write_text(json.dumps(report,indent=2))
u.log('EXTERIOR_EXPORT_COMPLETE')
