import unreal as u,json
for a in u.GameplayStatics.get_all_actors_of_class(world,u.CarlaWheeledVehicle):
 if 'BP_LincolnMKZ_Interior' not in a.get_name():continue
 for c in a.get_components_by_class(u.StaticMeshComponent):
  if c.get_name()=='BrochureDoorDetails_FL':c.set_relative_location(u.Vector(-95.953064,87.410208,-87.399002),False,True)
  if c.get_name()=='BrochureDoorDetails_FR':c.set_relative_location(u.Vector(-95.953064,-88.020967,-87.398903),False,True)
