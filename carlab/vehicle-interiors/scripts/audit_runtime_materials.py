import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');done=False

def inspect(delta):
 global done
 if done:return
 try:
  world=u.find_object(None,'/Game/Carla/Maps/Town10HD_Opt.Town10HD_Opt')
  if not world:return
  for actor in u.GameplayStatics.get_all_actors_of_class(world,u.CarlaWheeledVehicle):
   if 'BP_LincolnMKZ_Interior' not in actor.get_name():continue
   result={}
   for c in actor.get_components_by_class(u.MeshComponent):
    mats=[]
    for m in c.get_materials():
     entry={'name':m.get_path_name() if m else None}
     if isinstance(m,u.MaterialInstance):entry['parent']=str(m.get_editor_property('parent'))
     mats.append(entry)
    result[c.get_name()]={'materials':mats,'visible':c.get_editor_property('visible'),'hidden_in_game':c.get_editor_property('hidden_in_game')}
   (R/'actual-runtime-materials.json').write_text(json.dumps(result,indent=2))
   done=True
 except Exception as e:
  (R/'actual-runtime-materials-error.txt').write_text(str(e));done=True

handle=u.register_slate_post_tick_callback(inspect)
