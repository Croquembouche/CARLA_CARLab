import unreal as u
u.EditorLevelLibrary.load_level('/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio')
L=u.MaterialEditingLibrary;E=u.EditorAssetLibrary
bg=u.load_asset('/Game/VehicleExteriors/LincolnMKZ/M_MKZ_StudioBackdrop');L.delete_all_material_expressions(bg);bg.set_editor_property('two_sided',True);bg.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
n=L.create_material_expression(bg,u.MaterialExpressionConstant3Vector);n.constant=u.LinearColor(30,36,44,1);L.connect_material_property(n,'',u.MaterialProperty.MP_EMISSIVE_COLOR);L.recompile_material(bg);E.save_loaded_asset(bg,False)
floor=u.load_asset('/Game/VehicleExteriors/LincolnMKZ/M_MKZ_StudioFloor');L.delete_all_material_expressions(floor)
n=L.create_material_expression(floor,u.MaterialExpressionConstant3Vector);n.constant=u.LinearColor(.105,.12,.135,1);L.connect_material_property(n,'',u.MaterialProperty.MP_BASE_COLOR)
n=L.create_material_expression(floor,u.MaterialExpressionConstant);n.r=.85;L.connect_material_property(n,'',u.MaterialProperty.MP_ROUGHNESS);L.recompile_material(floor);E.save_loaded_asset(floor,False)
for a in u.EditorLevelLibrary.get_all_level_actors():
 if isinstance(a,u.SkyLight):a.light_component.set_editor_property('sky_distance_threshold',1000)
u.EditorLevelLibrary.save_current_level();u.log('STUDIO_LIGHTING_REFINED')
