"""Read-only transform audit in the owned test session."""
import unreal as u,json,time
from pathlib import Path
_door_audit_path=Path('/mnt/simulations/vehicle-interiors/door-motion-audit.json');_door_audit_last=0;_door_audit_records=[]
def audit_door_motion(delta):
 global _door_audit_last
 if time.monotonic()-_door_audit_last<1:return
 _door_audit_last=time.monotonic()
 try:
  world=u.find_object(None,'/Game/Carla/Maps/Town10HD_Opt.Town10HD_Opt')
  if not world:return
  for a in u.GameplayStatics.get_all_actors_of_class(world,u.CarlaWheeledVehicle):
   if 'BP_LincolnMKZ_Interior' not in a.get_name():continue
   data={}
   for c in a.get_components_by_class(u.StaticMeshComponent):
    if c.get_name() not in ['SM_LincolnDoor_Front_R','SM_LincolnDoor_Front_L','BrochureDoorDetails_FR','BrochureDoorDetails_FL']:continue
    parent=c.get_attach_parent();data[c.get_name()]={'parent':parent.get_name() if parent else None,'relative_location':str(c.get_editor_property('relative_location')),'relative_rotation':str(c.get_editor_property('relative_rotation')),'world_location':str(c.get_world_location()),'world_rotation':str(c.get_world_rotation()),'absolute_location':c.get_editor_property('absolute_location'),'absolute_rotation':c.get_editor_property('absolute_rotation'),'simulating':c.is_simulating_physics()}
   _door_audit_records.append(data);_door_audit_path.write_text(json.dumps(_door_audit_records[-100:],indent=2))
 except Exception as e:Path('/mnt/simulations/vehicle-interiors/door-motion-audit-error.txt').write_text(str(e))
_door_audit_handle=u.register_slate_post_tick_callback(audit_door_motion)
