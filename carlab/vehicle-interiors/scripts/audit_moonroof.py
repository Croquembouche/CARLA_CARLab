import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';r={};bp=u.load_asset(D+'BP_LincolnMKZ_Interior');a=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector())
for c in a.get_components_by_class(u.StaticMeshComponent):
 if c.get_name() not in ['MoonroofGlass','InteriorCompletion']:continue
 r[c.get_name()]={'materials':[str(m) for m in c.get_materials()],'transform':str(c.get_world_transform()),'visible':c.is_visible(),'hidden':c.get_editor_property('hidden_in_game'),'mesh':str(c.static_mesh)}
m=u.load_asset(D+'MI_MKZ_Moonroof_Tint');p=m.get_editor_property('base_property_overrides');r['override']={k:p.get_editor_property(k) for k in ['override_two_sided','two_sided']}
for name,ma in [('stock',m.parent),('custom',u.load_asset(D+'MKZ_Moonroof_Glass'))]:
 r[name]={k:str(ma.get_editor_property(k)) for k in ['blend_mode','two_sided','translucency_lighting_mode']}
 for label,prop in [('opacity',u.MaterialProperty.MP_OPACITY),('base',u.MaterialProperty.MP_BASE_COLOR)]:
  n=u.MaterialEditingLibrary.get_material_property_input_node(ma,prop);r[name][label]=str(n)
u.EditorLevelLibrary.destroy_actor(a);open('/mnt/simulations/vehicle-interiors/moonroof-audit.json','w').write(json.dumps(r,indent=2))
