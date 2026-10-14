import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';a=u.AssetToolsHelpers.get_asset_tools()
def material(name,color,rough=.5,metal=0,glass=False):
 m=u.load_asset(D+'/'+name)
 if m:return m
 m=a.create_asset(name,D,u.Material,u.MaterialFactoryNew());ed=u.MaterialEditingLibrary
 c=ed.create_material_expression(m,u.MaterialExpressionConstant3Vector);c.constant=u.LinearColor(*color,1);ed.connect_material_property(c,'',u.MaterialProperty.MP_BASE_COLOR)
 for value,prop in [(rough,u.MaterialProperty.MP_ROUGHNESS),(metal,u.MaterialProperty.MP_METALLIC)]+([(.32,u.MaterialProperty.MP_OPACITY)] if glass else []):
  n=ed.create_material_expression(m,u.MaterialExpressionConstant);n.r=value;ed.connect_material_property(n,'',prop)
 if glass:
  m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT);m.set_editor_property('two_sided',True);m.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE)
 ed.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m,False);return m
for n,c,r in [('MKZ_Completion_Rubber',(.009,.01,.012),.8),('MKZ_Completion_Headliner',(.12,.125,.13),.95),('MKZ_Completion_Lens',(.65,.67,.62),.25),('MKZ_Seatbelt_Webbing',(.043,.046,.049),.87)]:material(n,c,r)
material('MKZ_Moonroof_Glass',(.055,.09,.11),.06,0,True)
meshes={}
for name in ['SM_MKZ_Cabin_Final','SM_MKZ_Completion_Details','SM_MKZ_Moonroof_Glass','SM_MKZ_Driver_Worn_Belt']:
 opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=False;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=False
 t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.fbx'));t.destination_path=D;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;t.options=opts;a.import_asset_tasks([t]);m=u.load_asset(D+'/'+name);assert m
 n=m.get_editor_property('nanite_settings');n.enabled=False;m.set_editor_property('nanite_settings',n)
 for i,slot in enumerate(m.static_materials):
  label=str(slot.material_slot_name);path=('MI_MKZ_Brochure_Interior' if u.EditorAssetLibrary.does_asset_exist(D+'/MI_MKZ_Brochure_Interior') else 'MI_MKZ_Final_Interior') if 'MI_LincolnMKZ_Interior01' in label else label
  path='MI_MKZ_Moonroof_Tint' if label=='MKZ_Moonroof_Glass' and u.EditorAssetLibrary.does_asset_exist(D+'/MI_MKZ_Moonroof_Tint') else path
  ma=u.load_asset(D+'/'+path);assert ma,(name,label);m.set_material(i,ma)
 u.EditorAssetLibrary.save_loaded_asset(m,False);meshes[name]=m
bp=u.load_asset(D+'/BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
handles=sub.k2_gather_subobject_data_for_blueprint(bp);parent=None;existing={}
for h in handles:
 o=lib.get_object(lib.get_data(h));name=o.get_name().replace('_GEN_VARIABLE','');existing[name]=o
 if name=='VehicleMesh':parent=h
assert parent
for name,mesh in [('CabinReplacement','SM_MKZ_Cabin_Final'),('InteriorCompletion','SM_MKZ_Completion_Details'),('MoonroofGlass','SM_MKZ_Moonroof_Glass'),('DriverWornBelt','SM_MKZ_Driver_Worn_Belt')]:
 o=existing.get(name)
 if not o:
  h,reason=sub.add_new_subobject(u.AddNewSubobjectParams(parent_handle=parent,new_class=u.StaticMeshComponent,blueprint_context=bp));sub.rename_subobject(h,u.Text(name));o=lib.get_object(lib.get_data(h))
 o.set_static_mesh(meshes[mesh]);o.set_editor_property('override_materials',[]);o.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);o.set_editor_property('disallow_nanite',True)
 if name=='MoonroofGlass':o.set_cast_shadow(False)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
(R/'completion-import.json').write_text(json.dumps({n:{'bounds':str(m.get_bounds()),'materials':[str(x.material_interface) for x in m.static_materials]} for n,m in meshes.items()},indent=2))
