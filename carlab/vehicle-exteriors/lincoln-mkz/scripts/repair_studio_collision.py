import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');u.EditorLevelLibrary.load_level('/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio')
for a in u.EditorLevelLibrary.get_all_level_actors():
 if isinstance(a,u.StaticMeshActor)and a.static_mesh_component.static_mesh.get_name()=='Sphere':
  a.static_mesh_component.set_collision_profile_name(u.Name('NoCollision'));a.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
u.EditorLevelLibrary.save_current_level()
bp=u.load_asset('/Game/VehicleExteriors/LincolnMKZ/BP_LincolnMKZ_Refined');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp)or lib.get_object(data)
 if isinstance(o,u.StaticMeshComponent)and o.get_name().startswith('SM_MKZ_'):o.set_collision_profile_name(u.Name('NoCollision'));o.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
u.BlueprintEditorLibrary.compile_blueprint(bp);u.EditorAssetLibrary.save_loaded_asset(bp,False)
u.EditorLevelLibrary.load_level('/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio')
for a in u.EditorLevelLibrary.get_all_level_actors():
 if isinstance(a,u.StaticMeshActor)and a.static_mesh_component.static_mesh.get_name()=='Sphere':assert a.static_mesh_component.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
u.log('STUDIO_COLLISION_REPAIR_PASS')
