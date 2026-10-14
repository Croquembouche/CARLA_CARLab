"""Make the Lincoln moonroof glass a closed collision surface for rain and traces."""
import json
from pathlib import Path

import unreal as u


ROOT = Path('/mnt/simulations/vehicle-interiors')
DEST = '/Game/VehicleInteriors/Lincoln/'

mesh = u.load_asset(DEST + 'SM_MKZ_Moonroof_Glass')
blueprint = u.load_asset(DEST + 'BP_LincolnMKZ_Interior')
assert mesh and blueprint

# The glass is already a closed, solidified mesh. Using its triangles for simple
# queries preserves the curved panel and avoids a box that extends beyond the
# actual moonroof opening.
body_setup = mesh.get_editor_property('body_setup')
assert body_setup
body_setup.set_editor_property(
    'collision_trace_flag',
    u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,
)
u.EditorStaticMeshLibrary.remove_collisions(mesh)
assert u.EditorAssetLibrary.save_loaded_asset(mesh, False)

subsystem = u.get_engine_subsystem(u.SubobjectDataSubsystem)
library = u.SubobjectDataBlueprintFunctionLibrary
handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
component = None
for handle in handles:
    data = library.get_data(handle)
    obj = library.get_object_for_blueprint(data, blueprint) or library.get_object(data)
    if obj and obj.get_name().replace('_GEN_VARIABLE', '') == 'MoonroofGlass':
        component = obj
        break
assert component

component.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
component.set_collision_profile_name('BlockAll')
component.set_editor_property('generate_overlap_events', False)

u.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert u.EditorAssetLibrary.save_loaded_asset(blueprint, False)

actor = u.EditorLevelLibrary.spawn_actor_from_class(blueprint.generated_class(), u.Vector())
glass = next(
    c for c in actor.get_components_by_class(u.StaticMeshComponent)
    if c.get_name() == 'MoonroofGlass'
)
report = {
    'result': 'PASS_SAVED_BLUEPRINT',
    'blueprint': blueprint.get_path_name(),
    'component': glass.get_name(),
    'mesh': glass.static_mesh.get_path_name(),
    'collision_enabled': str(glass.get_collision_enabled()),
    'collision_profile': str(glass.get_collision_profile_name()),
    'collision_trace_flag': str(
        glass.static_mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')
    ),
    'bounds': str(glass.static_mesh.get_bounds()),
}
u.EditorLevelLibrary.destroy_actor(actor)
(ROOT / 'moonroof-collision-install.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
