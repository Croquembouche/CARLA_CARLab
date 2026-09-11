import unreal as u,json
from pathlib import Path
ROOT=Path('/mnt/simulations/vehicle-interiors');DEST='/Game/VehicleInteriors/Lincoln'
textures={}
for suffix in ['BaseColor','ORM']:
 name='T_MKZ_Ebony_'+suffix
 t=u.AssetImportTask();t.filename=str(ROOT/'exports'/(name+'.png'));t.destination_path=DEST;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True
 u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t]);tex=u.load_asset(DEST+'/'+name)
 tex.set_editor_property('srgb',suffix=='BaseColor')
 tex.set_editor_property('virtual_texture_streaming',False)
 if suffix=='ORM':tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_MASKS)
 u.EditorAssetLibrary.save_loaded_asset(tex);textures[suffix]=tex
name='MI_MKZ_Ebony_Interior'
m=u.load_asset(DEST+'/'+name) or u.EditorAssetLibrary.duplicate_asset('/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/Materials/MI_LincolnMKZ_Interior01',DEST+'/'+name)
u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(m,'Diffuse',textures['BaseColor'])
u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(m,'ORME',textures['ORM'])
u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(m,'Normal Intensity',.09)
u.MaterialEditingLibrary.update_material_instance(m);u.EditorAssetLibrary.save_loaded_asset(m)
bp=u.load_asset(DEST+'/BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
changed=[]
for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
 obj=lib.get_object(lib.get_data(handle))
 if isinstance(obj,u.MeshComponent):
  for i in range(obj.get_num_materials()):
   old=obj.get_material(i)
   if old and ('MI_LincolnMKZ_Interior01' in old.get_name() or old.get_name()==name):obj.set_material(i,m);changed.append([obj.get_name(),i])
u.BlueprintEditorLibrary.compile_blueprint(bp);u.EditorAssetLibrary.save_loaded_asset(bp,False)
(ROOT/'finish-import-report.json').write_text(json.dumps({'material':m.get_path_name(),'overrides':changed},indent=2))
u.log('MKZ_FINISH_APPLIED')
# Import the Blender-authored cabin as an attached component; retain the vehicle rig.
options=u.FbxImportUI();options.import_mesh=True;options.import_materials=False;options.import_textures=False;options.import_as_skeletal=False;options.automated_import_should_detect_type=False;options.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
options.static_mesh_import_data.combine_meshes=True;options.static_mesh_import_data.auto_generate_collision=False;options.static_mesh_import_data.convert_scene=True;options.static_mesh_import_data.convert_scene_unit=True
options.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
t=u.AssetImportTask();t.filename=str(ROOT/'exports/SM_MKZ_Cabin_v2.fbx');t.destination_path=DEST;t.destination_name='SM_MKZ_Cabin_v2';t.automated=True;t.replace_existing=True;t.save=True;t.options=options
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t]);cabin=u.load_asset(DEST+'/SM_MKZ_Cabin_v2')
finish_colors={'MKZ_Ebony_Leather':((.018,.021,.024),.4,0),'MKZ_Perforated_Leather':((.016,.019,.022),.48,0),'MKZ_Seat_Base_Plastic':((.012,.014,.018),.68,0),'MKZ_Seat_Hardware':((.5,.53,.57),.23,.85),'MKZ_Belt_Release':((.35,.008,.007),.5,0),'MKZ_Stitch_Charcoal':((.09,.092,.094),.76,0)}
for i,slot in enumerate(cabin.static_materials):
 slotname=str(slot.material_slot_name)
 if 'MI_LincolnMKZ_Interior01' in slotname:cabin.set_material(i,m);continue
 material=u.load_asset(DEST+'/'+slotname)
 if slotname in finish_colors and not material:
  color,rough,metal=finish_colors[slotname];material=u.AssetToolsHelpers.get_asset_tools().create_asset(slotname,DEST,u.Material,u.MaterialFactoryNew())
  node=u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionConstant3Vector);node.constant=u.LinearColor(*color,1);u.MaterialEditingLibrary.connect_material_property(node,'',u.MaterialProperty.MP_BASE_COLOR)
  for prop,val in [(u.MaterialProperty.MP_ROUGHNESS,rough),(u.MaterialProperty.MP_METALLIC,metal)]:
   node=u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionConstant);node.r=val;u.MaterialEditingLibrary.connect_material_property(node,'',prop)
  u.MaterialEditingLibrary.recompile_material(material);u.EditorAssetLibrary.save_loaded_asset(material)
 if material:cabin.set_material(i,material)
u.EditorAssetLibrary.save_loaded_asset(cabin)
hidden=u.load_asset(DEST+'/M_Hidden_StockCabin')
if not hidden:
 hidden=u.AssetToolsHelpers.get_asset_tools().create_asset('M_Hidden_StockCabin',DEST,u.Material,u.MaterialFactoryNew());hidden.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
 zero=u.MaterialEditingLibrary.create_material_expression(hidden,u.MaterialExpressionConstant);zero.r=0;u.MaterialEditingLibrary.connect_material_property(zero,'',u.MaterialProperty.MP_OPACITY_MASK)
 u.MaterialEditingLibrary.recompile_material(hidden);u.EditorAssetLibrary.save_loaded_asset(hidden)
hidden.set_editor_property('used_with_skeletal_mesh',True);hidden.set_editor_property('used_with_nanite',True);u.MaterialEditingLibrary.recompile_material(hidden);u.EditorAssetLibrary.save_loaded_asset(hidden)
handles=sub.k2_gather_subobject_data_for_blueprint(bp);objects={lib.get_object(lib.get_data(h)).get_name().replace('_GEN_VARIABLE',''):(h,lib.get_object(lib.get_data(h))) for h in handles}
root,vehicle=objects['VehicleMesh'];vehicle.set_material(2,hidden)
if 'CabinReplacement' in objects:comp=objects['CabinReplacement'][1]
else:
 h,reason=sub.add_new_subobject(u.AddNewSubobjectParams(parent_handle=root,new_class=u.StaticMeshComponent,blueprint_context=bp))
 if not lib.is_handle_valid(h):raise RuntimeError(str(reason))
 sub.rename_subobject(h,u.Text('CabinReplacement'));comp=lib.get_object(lib.get_data(h))
comp.set_static_mesh(cabin);comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
# Mats and seams are included in the replacement mesh.
if 'CabinDetails' in objects:objects['CabinDetails'][1].set_visibility(False)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
(ROOT/'finish-import-report.json').write_text(json.dumps({'material':m.get_path_name(),'overrides':changed,'cabin':cabin.get_path_name(),'bounds_cm':str(cabin.get_bounds()),'replacement_component':comp.get_name(),'seating':'new contoured geometry'},indent=2))
u.log('MKZ_CABIN_REPLACEMENT_COMPLETE')
