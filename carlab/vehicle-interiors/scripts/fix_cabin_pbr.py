import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';l=u.MaterialEditingLibrary
for texture_name in ['T_MKZ_Ebony_BaseColor','T_MKZ_Ebony_ORM']:
 texture=u.load_asset(D+texture_name)
 if texture.get_editor_property('virtual_texture_streaming'):
  texture.set_editor_property('virtual_texture_streaming',False)
  assert u.EditorAssetLibrary.save_loaded_asset(texture,False)
hidden=u.load_asset(D+'M_Hidden_StockCabin')
if not hidden.get_editor_property('used_with_nanite'):
 hidden.set_editor_property('used_with_nanite',True)
 l.recompile_material(hidden);assert u.EditorAssetLibrary.save_loaded_asset(hidden,False)
m=u.load_asset(D+'M_MKZ_Interior_PBR') or u.AssetToolsHelpers.get_asset_tools().create_asset('M_MKZ_Interior_PBR',D[:-1],u.Material,u.MaterialFactoryNew())
l.delete_all_material_expressions(m)
m.set_editor_property('used_with_skeletal_mesh',True)
m.set_editor_property('used_with_nanite',True)
for texname,pins in [('T_MKZ_Ebony_BaseColor',[('RGB',u.MaterialProperty.MP_BASE_COLOR)]),('T_MKZ_Ebony_ORM',[('R',u.MaterialProperty.MP_AMBIENT_OCCLUSION),('G',u.MaterialProperty.MP_ROUGHNESS),('B',u.MaterialProperty.MP_METALLIC)])]:
 n=l.create_material_expression(m,u.MaterialExpressionTextureSample);n.texture=u.load_asset(D+texname)
 n.sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR if 'BaseColor' in texname else u.MaterialSamplerType.SAMPLERTYPE_MASKS
 for pin,prop in pins:l.connect_material_property(n,pin,prop)
l.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m)
s=u.load_asset(D+'SM_MKZ_Cabin_v2');s.set_material(0,m)
# Explicitly support the imported static mesh render path.
for slot in s.static_materials:
 mat=slot.material_interface
 if isinstance(mat,u.Material) and mat != m and not mat.get_editor_property('used_with_nanite'):
  mat.set_editor_property('used_with_nanite',True);l.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat)
u.EditorAssetLibrary.save_loaded_asset(s)
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 obj=lib.get_object(lib.get_data(h))
 if isinstance(obj,u.MeshComponent):
  for i in range(obj.get_num_materials()):
   old=obj.get_material(i)
   if old and old.get_name()=='MI_MKZ_Ebony_Interior':obj.set_material(i,m)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
u.log('DIRECT_CABIN_PBR_COMPLETE')
