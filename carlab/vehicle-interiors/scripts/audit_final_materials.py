import unreal as u,json
D='/Game/VehicleInteriors/Lincoln/';r={};lib=u.MaterialEditingLibrary
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');a=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector())
for c in a.get_components_by_class(u.MeshComponent):
 r[c.get_name()]={'materials':[m.get_path_name() if m else None for m in c.get_materials()],'scale':str(c.get_world_scale()),'bounds':str(c.get_local_bounds()) if isinstance(c,u.StaticMeshComponent) else '','collision':str(c.get_collision_enabled())}
m=u.load_asset(D+'M_MKZ_Interior_PBR');r['pbr']={}
for name,prop in [('base_color',u.MaterialProperty.MP_BASE_COLOR),('roughness',u.MaterialProperty.MP_ROUGHNESS)]:
 node=lib.get_material_property_input_node(m,prop);r['pbr'][name]={'node':str(node),'texture':str(node.texture) if node and isinstance(node,u.MaterialExpressionTextureSample) else None}
u.EditorLevelLibrary.destroy_actor(a)
open('/mnt/simulations/vehicle-interiors/final-material-audit.json','w').write(json.dumps(r,indent=2))
