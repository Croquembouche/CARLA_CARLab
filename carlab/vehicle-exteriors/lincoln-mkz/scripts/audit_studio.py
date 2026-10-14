import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');u.EditorLevelLibrary.load_level('/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio')
report=[]
for a in u.EditorLevelLibrary.get_all_level_actors():
 if isinstance(a,u.StaticMeshActor):
  c=a.static_mesh_component;report.append({'name':a.get_name(),'mesh':c.static_mesh.get_path_name(),'location':str(a.get_actor_location()),'scale':str(a.get_actor_scale3d()),'collision_enabled':str(c.get_collision_enabled()),'collision_profile':str(c.get_collision_profile_name()),'bounds':str(a.get_actor_bounds(False))})
(R/'studio-audit.json').write_text(json.dumps(report,indent=2));u.log('STUDIO_AUDIT_DONE')
