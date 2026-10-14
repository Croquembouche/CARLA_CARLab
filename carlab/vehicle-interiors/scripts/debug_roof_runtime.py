import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors')
def roof_tick(delta):
 p=R/'.roof-debug-request'
 if not p.exists():return
 world=u.find_object(None,'/Game/Carla/Maps/Town10HD_Opt.Town10HD_Opt')
 for actor in u.GameplayStatics.get_all_actors_of_class(world,u.CarlaWheeledVehicle):
  if 'BP_LincolnMKZ_Interior' not in actor.get_name():continue
  for c in actor.get_components_by_class(u.StaticMeshComponent):
   if c.get_name()!='MoonroofGlass':continue
   mode=p.read_text().strip();m=u.load_asset('/Game/VehicleInteriors/Lincoln/'+('MKZ_Completion_Lens' if mode=='opaque' else 'MI_MKZ_Moonroof_Tint'));c.set_material(0,m);c.set_visibility(mode!="off")
   r={'mode':mode,'transform':str(c.get_world_transform()),'relative':str(c.get_relative_transform()),'actor':str(actor.get_actor_transform()),'material':str(m)}
   for k in ['render_in_main_pass','owner_no_see','only_owner_see','visible_in_scene_capture_only','hidden_in_scene_capture']:
    try:r[k]=str(c.get_editor_property(k))
    except:pass
   r['cameras']=[]
   for camera in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
    for cc in camera.get_components_by_class(u.SceneCaptureComponent2D):
     r['cameras'].append({'flags':str(cc.get_editor_property('show_flag_settings')),'pp':str(cc.get_editor_property('post_process_settings').get_editor_property('translucency_type'))})
   (R/'roof-debug-status.json').write_text(json.dumps(r,indent=2));p.unlink();return
roof_handle=u.register_slate_post_tick_callback(roof_tick)
