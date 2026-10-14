import unreal as u,json
from pathlib import Path
D='/Game/VehicleInteriors/Lincoln/';e=u.MaterialEditingLibrary;r={}
for name in ['MI_MKZ_Brochure_Interior','MKZ_Ebony_Leather','MKZ_Perforated_Leather','MKZ_Fine_Satin','MKZ_Brochure_Satin','MKZ_Brochure_Chrome','MKZ_Speaker_Metal']:
 m=u.load_asset(D+name);q={'class':m.get_class().get_name()}
 if isinstance(m,u.Material):
  for prop in [u.MaterialProperty.MP_BASE_COLOR,u.MaterialProperty.MP_ROUGHNESS,u.MaterialProperty.MP_METALLIC,u.MaterialProperty.MP_SPECULAR,u.MaterialProperty.MP_NORMAL]:
   n=e.get_material_property_input_node(m,prop);d={'node':str(n)}
   if isinstance(n,u.MaterialExpressionConstant):d['value']=n.r
   if isinstance(n,u.MaterialExpressionConstant3Vector):d['value']=str(n.constant)
   if isinstance(n,u.MaterialExpressionTextureSample):d['texture']=n.texture.get_path_name();d['srgb']=n.texture.get_editor_property('srgb')
   q[str(prop)]=d
 else:
  q['parent']=str(m.get_editor_property('parent'));q['scalars']={str(n):e.get_material_instance_scalar_parameter_value(m,n) for n in e.get_scalar_parameter_names(m)}
  q['switches']={str(n):e.get_material_instance_static_switch_parameter_value(m,n) for n in e.get_static_switch_parameter_names(m)}
 r[name]=q
Path('/mnt/simulations/vehicle-interiors/materials-v3-audit.json').write_text(json.dumps(r,indent=2))
