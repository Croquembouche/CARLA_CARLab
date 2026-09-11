"""Run with Unreal's PythonScript commandlet. Read/export stock assets only."""
import json
from pathlib import Path
import unreal as u

ROOT = Path('/mnt/simulations/vehicle-interiors')
OUT = ROOT / 'reference'
OUT.mkdir(parents=True, exist_ok=True)
base = '/Game/Carla/Static/Car/4Wheeled/LincolnMKZ'
report = {'assets': [], 'components': [], 'exports': []}
for path in u.EditorAssetLibrary.list_assets(base, recursive=True, include_folder=False):
    asset = u.load_asset(path)
    entry = {'path': path, 'class': asset.get_class().get_name()}
    if isinstance(asset, u.SkeletalMesh):
        entry['materials'] = [str(x.material_interface.get_path_name()) if x.material_interface else None for x in asset.materials]
    report['assets'].append(entry)
    if isinstance(asset, (u.SkeletalMesh, u.Texture2D)) or ('/Meshes/Doors/' in path and isinstance(asset, u.StaticMesh)):
        ext = '.fbx' if isinstance(asset, (u.SkeletalMesh, u.StaticMesh)) else '.tga'
        task = u.AssetExportTask()
        task.object = asset
        task.filename = str(OUT / (asset.get_name() + ext))
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        if ext == '.fbx':
            task.options = u.FbxExportOption()
            task.options.ascii = False
            task.options.level_of_detail = False
        report['exports'].append({'file': task.filename, 'ok': u.Exporter.run_asset_export_task(task)})
bp = u.load_asset('/Game/Carla/Blueprints/Vehicles/LincolnMKZ/BP_LincolnMKZ')
sub = u.get_engine_subsystem(u.SubobjectDataSubsystem)
for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
    data = u.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    obj = u.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    entry = {'name': obj.get_name(), 'class': obj.get_class().get_name()}
    for key in ('relative_location', 'relative_rotation', 'relative_scale3d', 'static_mesh', 'skeletal_mesh'):
        try: entry[key] = str(obj.get_editor_property(key))
        except Exception: pass
    report['components'].append(entry)
(OUT / 'audit.json').write_text(json.dumps(report, indent=2))
u.log('LINCOLN_AUDIT_COMPLETE')
