import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';a=u.AssetToolsHelpers.get_asset_tools()
source=(R/'scripts/import_completion.py').read_text();exec(compile(source[:source.index('meshes={}')],'material-helpers','exec'));material('MKZ_Speaker_Metal',(.13,.145,.16),.46,.72)
bp=u.load_asset(D+'/BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary;existing={};handles={}
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp) or lib.get_object(data);name=o.get_name().replace('_GEN_VARIABLE','');existing[name]=o;handles[name]=h
report=[]
for code,parentname in [('FL','SM_LincolnDoor_Front_L'),('FR','SM_LincolnDoor_Front_R')]:
 name='SM_MKZ_Door_Details_'+code;opts=u.FbxImportUI();opts.import_mesh=True;opts.import_materials=False;opts.import_textures=False;opts.import_as_skeletal=False;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opts.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;opts.static_mesh_import_data.combine_meshes=True;opts.static_mesh_import_data.auto_generate_collision=False
 t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.fbx'));t.destination_path=D;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;t.options=opts;a.import_asset_tasks([t]);mesh=u.load_asset(D+'/'+name);assert mesh
 for i,slot in enumerate(mesh.static_materials):
  mat=u.load_asset(D+'/'+str(slot.material_slot_name));assert mat,slot.material_slot_name;mesh.set_material(i,mat)
 assert u.EditorAssetLibrary.save_loaded_asset(mesh,False)
 cname='BrochureDoorDetails_'+code;o=existing.get(cname)
 if not o:
  h,reason=sub.add_new_subobject(u.AddNewSubobjectParams(parent_handle=handles[parentname],new_class=u.StaticMeshComponent,blueprint_context=bp));assert h,reason;sub.rename_subobject(h,u.Text(cname));data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp) or lib.get_object(data)
 o.set_static_mesh(mesh);o.set_editor_property('override_materials',[]);o.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);hinges={b['name']:b['head'] for b in json.loads((R/'reference/blender-audit.json').read_text())[0]['bones']};hx,hy,hz=hinges['Door_Front_'+('Left' if code=='FL' else 'Right')];o.set_editor_property('relative_location',u.Vector(-hx*100,hy*100,-hz*100));o.set_editor_property('relative_rotation',u.Rotator(0,0,0));o.set_editor_property('relative_scale3d',u.Vector(1,1,1));o.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
 report.append({'component':cname,'parent':parentname,'mesh':mesh.get_path_name(),'bounds':str(mesh.get_bounds()),'relative_location':str(o.get_editor_property('relative_location'))})
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
(R/'brochure-doors-import.json').write_text(json.dumps({'result':'IMPORTED_AND_BLUEPRINT_COMPILED','components':report,'open_door_visual_check':'PENDING'},indent=2))
