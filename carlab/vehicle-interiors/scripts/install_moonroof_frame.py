"""Import the roof surround and install it in BP_LincolnMKZ_Interior."""
import json
from pathlib import Path
import unreal as u

ROOT = Path('/mnt/simulations/vehicle-interiors')
DEST = '/Game/VehicleInteriors/Lincoln/'
ASSET = 'SM_MKZ_Moonroof_Frame'

options = u.FbxImportUI()
options.import_mesh = True
options.import_materials = False
options.import_textures = False
options.import_as_skeletal = False
options.automated_import_should_detect_type = False
options.mesh_type_to_import = u.FBXImportType.FBXIT_STATIC_MESH
options.static_mesh_import_data.combine_meshes = True
options.static_mesh_import_data.auto_generate_collision = False
options.static_mesh_import_data.normal_import_method = u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS

task = u.AssetImportTask()
task.filename = str(ROOT / 'exports' / f'{ASSET}.fbx')
task.destination_path = DEST.rstrip('/')
task.destination_name = ASSET
task.automated = True
task.replace_existing = True
task.save = True
task.options = options
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

mesh = u.load_asset(DEST + ASSET)
bodywork = u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Materials/MI_LincolnMKZ_Bodywork')
headliner = u.load_asset(DEST + 'MKZ_Completion_Headliner')
assert mesh and bodywork and headliner
nanite = mesh.get_editor_property('nanite_settings')
nanite.enabled = False
mesh.set_editor_property('nanite_settings', nanite)
assert len(mesh.static_materials) == 2, len(mesh.static_materials)
mesh.set_material(0, bodywork)
mesh.set_material(1, headliner)
u.EditorAssetLibrary.save_loaded_asset(mesh, False)

blueprint = u.load_asset(DEST + 'BP_LincolnMKZ_Interior')
subsystem = u.get_engine_subsystem(u.SubobjectDataSubsystem)
library = u.SubobjectDataBlueprintFunctionLibrary
handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
root_handle = handles[0]
component = None
for handle in handles:
    data = library.get_data(handle)
    obj = library.get_object_for_blueprint(data, blueprint) or library.get_object(data)
    if obj and obj.get_name().replace('_GEN_VARIABLE', '') == 'MoonroofFrame':
        component = obj
        break
if component is None:
    params = u.AddNewSubobjectParams(
        parent_handle=root_handle,
        new_class=u.StaticMeshComponent,
        blueprint_context=blueprint,
    )
    new_handle, fail_reason = subsystem.add_new_subobject(params=params)
    assert library.is_handle_valid(new_handle), str(fail_reason)
    subsystem.rename_subobject(new_handle, u.Text('MoonroofFrame'))
    data = library.get_data(new_handle)
    component = library.get_object_for_blueprint(data, blueprint) or library.get_object(data)

component.set_static_mesh(mesh)
component.set_editor_property('override_materials', [])
component.set_editor_property('disallow_nanite', True)
component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
component.set_cast_shadow(True)

u.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert u.EditorAssetLibrary.save_loaded_asset(blueprint, False)

actor = u.EditorLevelLibrary.spawn_actor_from_class(blueprint.generated_class(), u.Vector())
components = {obj.get_name(): obj for obj in actor.get_components_by_class(u.StaticMeshComponent)}
assert components['MoonroofFrame'].static_mesh == mesh
assert components['MoonroofGlass'].static_mesh == u.load_asset(DEST + 'SM_MKZ_Moonroof_Glass')
assert components['MoonroofFrame'].get_material(0) == bodywork
assert components['MoonroofFrame'].get_material(1) == headliner
report = {
    'result': 'PASS_SAVED_BLUEPRINT',
    'blueprint': blueprint.get_path_name(),
    'component': 'MoonroofFrame',
    'frame_mesh': mesh.get_path_name(),
    'exterior_material': bodywork.get_path_name(),
    'interior_material': headliner.get_path_name(),
    'moonroof_glass': components['MoonroofGlass'].static_mesh.get_path_name(),
    'bounds': str(mesh.get_bounds()),
}
u.EditorLevelLibrary.destroy_actor(actor)
(ROOT / 'moonroof-frame-install.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
