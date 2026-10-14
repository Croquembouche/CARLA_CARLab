import unreal as u,json
from pathlib import Path
D='/Game/VehicleInteriors/Lincoln/L_MKZ_Photography';R=Path('/mnt/simulations/vehicle-interiors')
if u.EditorAssetLibrary.does_asset_exist(D):assert u.EditorLevelLibrary.load_level(D)
else:assert u.EditorLevelLibrary.new_level(D)
existing={o.get_actor_label():o for o in u.EditorLevelLibrary.get_all_level_actors() if isinstance(o,u.RectLight)}
world=u.EditorLevelLibrary.get_editor_world();result=[]
for label,loc,target,intensity,width,height in [('PassengerSoftbox',(5,145,130),(-15,0,80),3.2,130,100),('WindshieldSoftbox',(165,0,150),(10,0,75),4.5,150,100),('RearFill',(-165,-30,140),(-35,0,85),2,120,85)]:
 pos=u.Vector(*loc);rot=u.MathLibrary.find_look_at_rotation(pos,u.Vector(*target));lamp=existing.get(label) or u.EditorLevelLibrary.spawn_actor_from_class(u.RectLight,pos,rot);assert lamp;lamp.set_actor_label(label)
 comp=lamp.get_component_by_class(u.RectLightComponent);comp.set_mobility(u.ComponentMobility.MOVABLE);comp.set_intensity_units(u.LightUnits.LUMENS);comp.set_intensity(intensity);comp.set_source_width(width);comp.set_source_height(height);comp.set_attenuation_radius(450);comp.set_light_color(u.LinearColor(1,.96,.90,1));result.append(label)
assert u.EditorLoadingAndSavingUtils.save_map(world,D)
(R/'photography-level-build.json').write_text(json.dumps({'result':'SAVED','level':D,'lights':result,'usage':'optional photographic reference capture only'}))
