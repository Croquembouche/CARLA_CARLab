import unreal as u,json
from pathlib import Path
D='/Game/VehicleInteriors/Lincoln/';bp=u.load_asset(D+'BP_LincolnMKZ_Interior');actor=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector())
out={}
for c in actor.get_components_by_class(u.MeshComponent):
 out[c.get_name()]={'materials':[str(c.get_material(i).get_path_name()) if c.get_material(i) else None for i in range(c.get_num_materials())],'visible':bool(c.get_editor_property('visible')),'relative_location':str(c.get_editor_property('relative_location'))}
sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);obj=lib.get_object(data)
 if obj.get_name()=='VehicleMesh':
  alt=lib.get_object_for_blueprint(data,bp);out['bp_specific_mesh']=str(alt)
  if alt:out['bp_specific_overrides']=[str(m) for m in alt.get_editor_property('override_materials')]
u.EditorLevelLibrary.destroy_actor(actor)
Path('/mnt/simulations/vehicle-interiors/constructed-vehicle-audit.json').write_text(json.dumps(out,indent=2))
