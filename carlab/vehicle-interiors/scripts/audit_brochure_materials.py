import unreal as u,json
from pathlib import Path
D='/Game/VehicleInteriors/Lincoln/';e=u.MaterialEditingLibrary;r={}
for name in ['MKZ_Ebony_Leather','MKZ_Perforated_Leather','MI_MKZ_Final_Interior','MKZ_Stitch_Charcoal']:
 m=u.load_asset(D+name);q={'class':str(type(m))}
 if isinstance(m,u.Material):
  for prop in [u.MaterialProperty.MP_BASE_COLOR,u.MaterialProperty.MP_ROUGHNESS,u.MaterialProperty.MP_METALLIC,u.MaterialProperty.MP_SPECULAR,u.MaterialProperty.MP_NORMAL]:
   n=e.get_material_property_input_node(m,prop);q[str(prop)]=str(n)
   if isinstance(n,u.MaterialExpressionConstant):q[str(prop)+'_value']=n.r
   if isinstance(n,u.MaterialExpressionConstant3Vector):q[str(prop)+'_value']=str(n.constant)
 else:
  q['scalars']={str(n):e.get_material_instance_scalar_parameter_value(m,n) for n in e.get_scalar_parameter_names(m)}
  q['vectors']={str(n):str(e.get_material_instance_vector_parameter_value(m,n)) for n in e.get_vector_parameter_names(m)}
 r[name]=q
Path('/mnt/simulations/vehicle-interiors/brochure-material-audit.json').write_text(json.dumps(r,indent=2))
