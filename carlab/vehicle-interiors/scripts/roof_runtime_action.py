import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');spec=json.loads((R/'.roof-debug-request').read_text());mode=spec['mode'];world=u.find_object(None,'/Game/Carla/Maps/Town10HD_Opt.Town10HD_Opt');r={'mode':mode}
for actor in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
 for c in actor.get_components_by_class(u.StaticMeshComponent):
  if c.get_name()=='SM_Glass_Lincoln_Ext1' and spec.get('stock_mesh'):
   mesh=u.load_asset('/Game/VehicleInteriors/Lincoln/'+spec['stock_mesh']);assert mesh;c.set_static_mesh(mesh)
  if c.get_name()!='MoonroofGlass':
   if 'glass' in c.get_name().lower():c.set_visibility(not (spec.get('hide_stock',False) or (spec.get('hide_main_stock',False) and c.get_name()=='SM_Glass_Lincoln_Ext1')))
   continue
  c.set_translucent_sort_priority(int(spec.get('sort_priority',0)))
  c.set_visibility(mode!='off');path=spec.get('material','MI_MKZ_Moonroof_Tint');m=u.load_asset('/Game/VehicleInteriors/Lincoln/'+path);assert m,path;c.set_material(0,m)
  if mode=='red':
   mi=c.create_dynamic_material_instance(0);mi.set_scalar_parameter_value('Opacity',.99);mi.set_vector_parameter_value('Color',u.LinearColor(1,0,0,1))
  r['material']=str(c.get_material(0))
(R/'roof-debug-status.json').write_text(json.dumps(r,indent=2))
