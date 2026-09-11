import unreal as u,json
from pathlib import Path
out={}
for name in ['MI_LincolnMKZ_Interior01','MI_LincolnMKZ_Interior2']:
 m=u.load_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Materials/'+name)
 out[name]={'parent':str(m.parent.get_path_name())}
 for field in ['scalar_parameter_values','vector_parameter_values','texture_parameter_values']:
  out[name][field]=[str(x) for x in m.get_editor_property(field)]
 parent=m.parent
 out[name]['inherited_vectors']={str(n):str(u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(m,n)) for n in u.MaterialEditingLibrary.get_vector_parameter_names(parent)}
 out[name]['inherited_scalars']={str(n):str(u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(m,n)) for n in u.MaterialEditingLibrary.get_scalar_parameter_names(parent)}
Path('/mnt/simulations/vehicle-interiors/reference/materials.json').write_text(json.dumps(out,indent=2))
