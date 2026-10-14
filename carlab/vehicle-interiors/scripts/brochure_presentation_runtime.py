"""Optional photographic sublevel in the isolated comparison world; no vehicle edits."""
import unreal as u,os,json,time
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');lit=set();photo_levels=[];seen_at={}
if os.environ.get('LINCOLN_BROCHURE_CAPTURE')=='1':
 def presentation(delta):
  try:
   world=u.find_object(None,'/Game/Carla/Maps/Town10HD_Opt.Town10HD_Opt')
   if not world:return
   action=R/'.brochure-action-request'
   if os.environ.get('LINCOLN_BROCHURE_LAB')=='1' and action.exists():
    action.unlink();exec(compile((R/'scripts/brochure_runtime_action.py').read_text(),'brochure_runtime_action.py','exec'),globals(),{'world':world})
   tuning=R/'.brochure-light-tuning.json'
   if os.environ.get('LINCOLN_BROCHURE_LAB')=='1' and tuning.exists():
    values=json.loads(tuning.read_text());lamps=sorted([l for l in u.GameplayStatics.get_all_actors_of_class(world,u.RectLight) if 'L_MKZ_Photography' in l.get_path_name()],key=lambda l:l.get_name())
    for index,lamp in enumerate(lamps):
     if index<len(values):lamp.get_component_by_class(u.RectLightComponent).set_intensity(float(values[index]))
   for a in u.GameplayStatics.get_all_actors_of_class(world,u.CarlaWheeledVehicle):
    if 'BP_LincolnMKZ_Interior' not in a.get_name():continue
    for c in a.get_components_by_class(u.MeshComponent):
     if isinstance(c,u.PoseableMeshComponent) or c.get_name().startswith('DriverWornBelt'):c.set_visibility(False);c.set_hidden_in_game(True)
    if a.get_name() in lit:continue
    seen_at.setdefault(a.get_name(),time.monotonic())
    if time.monotonic()-seen_at[a.get_name()]<3:continue
    level,success=u.LevelStreamingDynamic.load_level_instance(world,'/Game/VehicleInteriors/Lincoln/L_MKZ_Photography',a.get_actor_location(),a.get_actor_rotation());assert success
    photo_levels.append(level);lit.add(a.get_name())
    (R/'brochure-lighting-status.json').write_text(json.dumps({'level':level.get_path_name(),'temporary':True}))
    (R/'brochure-lighting-error.txt').unlink(missing_ok=True)
  except Exception as ex:
   (R/'brochure-lighting-error.txt').write_text(str(ex))
 handle_presentation=u.register_slate_post_tick_callback(presentation)
