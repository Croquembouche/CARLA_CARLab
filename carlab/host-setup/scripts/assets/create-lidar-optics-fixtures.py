import unreal
lib=unreal.MaterialEditingLibrary
tools=unreal.AssetToolsHelpers.get_asset_tools()
folder='/Game/Carla/Testing/Optics'
def mat(name,color,metallic=0,roughness=.6,glass=False,emissive=False):
 p=folder+'/'+name
 m=unreal.load_asset(p) if unreal.EditorAssetLibrary.does_asset_exist(p) else tools.create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
 lib.delete_all_material_expressions(m)
 m.set_editor_property('two_sided',True)
 if glass:
  m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
  m.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
 def scalar(prop,value):
  e=lib.create_material_expression(m,unreal.MaterialExpressionConstant);e.set_editor_property('r',value);lib.connect_material_property(e,'',prop)
 e=lib.create_material_expression(m,unreal.MaterialExpressionConstant3Vector);e.set_editor_property('constant',unreal.LinearColor(*color));lib.connect_material_property(e,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR if emissive else unreal.MaterialProperty.MP_BASE_COLOR)
 scalar(unreal.MaterialProperty.MP_METALLIC,metallic);scalar(unreal.MaterialProperty.MP_ROUGHNESS,roughness)
 if glass:
  scalar(unreal.MaterialProperty.MP_OPACITY,.08);scalar(unreal.MaterialProperty.MP_REFRACTION,1.5)
 lib.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
 return p
paths=[mat('M_CarlaOptics_Concrete',(.35,.35,.35,1)),mat('M_CarlaOptics_RoadMarking',(.8,.8,.8,1)),mat('M_CarlaOptics_Sign',(.5,.5,.5,1)),mat('M_CarlaOptics_Mirror',(.95,.95,.95,1),1,0),mat('M_CarlaOptics_Glass',(.95,.98,1,1),0,0,True),mat('M_CarlaOptics_Red',(4,0,0,1),emissive=True),mat('M_CarlaOptics_Green',(0,4,0,1),emissive=True)]
unreal.log("CARLA_OPTICS_ASSETS_DONE")
