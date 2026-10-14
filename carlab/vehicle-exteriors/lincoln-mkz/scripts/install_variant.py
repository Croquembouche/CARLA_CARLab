"""Install a separately registered exterior variant; never overwrite stock assets."""
import unreal as u,json,hashlib
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');D='/Game/VehicleExteriors/LincolnMKZ';B='/Game/Carla/Static/Car/4Wheeled/LincolnMKZ'
E=u.EditorAssetLibrary;A=u.AssetToolsHelpers.get_asset_tools();L=u.MaterialEditingLibrary
E.make_directory(D)
report={'blueprint':D+'/BP_LincolnMKZ_Refined','carla_id':'vehicle.lincoln.mkz_refined','materials':{},'meshes':{},'components':[]}
def scalar(m,value,prop):
    n=L.create_material_expression(m,u.MaterialExpressionConstant);n.r=value;assert L.connect_material_property(n,'',prop)
def material(name,c,metal,rough):
    m=u.load_asset(D+'/'+name)or A.create_asset(name,D,u.Material,u.MaterialFactoryNew());L.delete_all_material_expressions(m)
    n=L.create_material_expression(m,u.MaterialExpressionVectorParameter);n.set_editor_property('parameter_name',u.Name('Base Color'));n.set_editor_property('default_value',u.LinearColor(*c,1));L.connect_material_property(n,'',u.MaterialProperty.MP_BASE_COLOR)
    scalar(m,metal,u.MaterialProperty.MP_METALLIC);scalar(m,rough,u.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property('used_with_skeletal_mesh',True);m.set_editor_property('used_with_nanite',True);L.recompile_material(m);E.save_loaded_asset(m,False)
    report['materials'][name]=m.get_path_name();return m
materials={
 'M_MKZ_Fastener':material('M_MKZ_Fastener',(.36,.39,.42),1,.28),
 'M_MKZ_ValveRubber':material('M_MKZ_ValveRubber',(.009,.012,.015),0,.68),
 'M_MKZ_BrakeSteel':material('M_MKZ_BrakeSteel',(.24,.25,.26),1,.32),
}
paint=u.load_asset(D+'/MI_MKZ_ClearcoatPaint')or E.duplicate_asset(B+'/Materials/MI_LincolnMKZ_Bodywork',D+'/MI_MKZ_ClearcoatPaint')
for name,value in [('Metallic',.5),('Roughness',.19),('CC Roughness',.075)]:L.set_material_instance_scalar_parameter_value(paint,name,value)
L.set_material_instance_vector_parameter_value(paint,'Base Color',u.LinearColor(.024,.045,.065,1));L.update_material_instance(paint);E.save_loaded_asset(paint,False);report['materials']['paint']=paint.get_path_name()
for name in ['T_MKZ_Exterior02_BaseColor','T_MKZ_Exterior02_ORM']:
    t=u.AssetImportTask();t.filename=str(R/'exports'/(name+'.png'));t.destination_path=D;t.destination_name=name;t.automated=True;t.replace_existing=True;t.save=True;A.import_asset_tasks([t]);tex=u.load_asset(D+'/'+name);assert tex
    tex.set_editor_property('srgb',name.endswith('BaseColor'));tex.set_editor_property('virtual_texture_streaming',False)
    if name.endswith('ORM'):tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_MASKS)
    E.save_loaded_asset(tex,False)
trim=u.load_asset(D+'/MI_MKZ_ExteriorTrim')or E.duplicate_asset(B+'/Materials/MI_LincolnMKZ_Interior2',D+'/MI_MKZ_ExteriorTrim')
L.set_material_instance_texture_parameter_value(trim,'Diffuse',u.load_asset(D+'/T_MKZ_Exterior02_BaseColor'))
L.set_material_instance_texture_parameter_value(trim,'ORME',u.load_asset(D+'/T_MKZ_Exterior02_ORM'));E.save_loaded_asset(trim,False)
glass=u.load_asset(D+'/MI_MKZ_ExteriorGlass')or E.duplicate_asset(B+'/Materials/MI_LincolnMKZ_Glass_Ext',D+'/MI_MKZ_ExteriorGlass')
# Preserve the stock glass shader and its reflection/refraction behavior.
L.set_material_instance_vector_parameter_value(glass,'Color',u.LinearColor(.015,.022,.029,1));L.set_material_instance_scalar_parameter_value(glass,'Opacity',.42);L.set_material_instance_scalar_parameter_value(glass,'Roughness',.018);L.update_material_instance(glass)
E.save_loaded_asset(glass,False)
for f in sorted((R/'exports').glob('SM_*.fbx')):
    opt=u.FbxImportUI();opt.import_mesh=True;opt.import_materials=False;opt.import_textures=False;opt.import_as_skeletal=False;opt.automated_import_should_detect_type=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opt.static_mesh_import_data.combine_meshes=True;opt.static_mesh_import_data.auto_generate_collision=False;opt.static_mesh_import_data.convert_scene=True;opt.static_mesh_import_data.convert_scene_unit=True;opt.static_mesh_import_data.normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    t=u.AssetImportTask();t.filename=str(f);t.destination_path=D;t.destination_name=f.stem;t.automated=True;t.replace_existing=True;t.save=True;t.options=opt;A.import_asset_tasks([t]);mesh=u.load_asset(D+'/'+f.stem);assert mesh
    for i,slot in enumerate(mesh.static_materials):
        name=str(slot.material_slot_name)
        if name in materials:mesh.set_material(i,materials[name])
        else:raise RuntimeError('Unmapped material '+name)
    ns=mesh.get_editor_property('nanite_settings');ns.enabled=False;mesh.set_editor_property('nanite_settings',ns);E.save_loaded_asset(mesh,False);report['meshes'][f.stem]={'path':mesh.get_path_name(),'bounds':str(mesh.get_bounds())}
bp=u.load_asset(report['blueprint'])or E.duplicate_asset('/Game/Carla/Blueprints/Vehicles/LincolnMKZ/BP_LincolnMKZ',report['blueprint'])
sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary;objects={}
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
    data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp)or lib.get_object(data);objects[o.get_name().replace('_GEN_VARIABLE','')]=(h,o)
root=next((h for n,(h,o)in objects.items()if n=='VehicleMesh'),None);assert root,'VehicleMesh not found'
for name,(h,o)in objects.items():
    if not isinstance(o,u.MeshComponent):continue
    mats=[]
    for m in o.get_materials():
        mn=m.get_name()if m else ''
        mats.append(paint if 'Bodywork'in mn else trim if mn=='MI_LincolnMKZ_Interior2'else glass if mn=='MI_LincolnMKZ_Glass_Ext'else m)
    if mats:o.set_editor_property('override_materials',mats)
for name in report['meshes']:
    if name in objects:h,o=objects[name]
    else:
        h,reason=sub.add_new_subobject(u.AddNewSubobjectParams(parent_handle=root,new_class=u.StaticMeshComponent,blueprint_context=bp));assert lib.is_handle_valid(h),str(reason);sub.rename_subobject(h,u.Text(name));data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp)or lib.get_object(data)
    o.set_static_mesh(u.load_asset(D+'/'+name));o.set_collision_profile_name(u.Name('NoCollision'));o.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);o.set_editor_property('mobility',u.ComponentMobility.MOVABLE);o.set_editor_property('relative_location',u.Vector());o.set_editor_property('relative_rotation',u.Rotator());o.set_editor_property('disallow_nanite',True)
    socket=name.split('SM_MKZ_Hardware_')[-1]if 'Hardware'in name else name.split('SM_MKZ_Rotor_')[-1]
    assert u.VehicleExteriorEditorLibrary.set_detail_socket(bp,u.Name(name),u.Name(socket)),('Unable to set persistent socket',name,socket)
    report['components'].append({'name':name,'socket':socket})
u.BlueprintEditorLibrary.compile_blueprint(bp);assert E.save_loaded_asset(bp,False)
# An isolated lightweight studio map allows fresh native validation without touching the live town.
lab=D+'/L_ExteriorStudio';u.EditorLevelLibrary.new_level(lab)
floor=u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-6));floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(u.Vector(100,100,.1));floor.static_mesh_component.set_material(0,material('M_MKZ_StudioFloor',(.105,.12,.135),0,.85))
sky=u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor,u.Vector());sky.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Sphere'));sky.set_actor_scale3d(u.Vector(500,500,500));sky.static_mesh_component.set_collision_profile_name(u.Name('NoCollision'));sky.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);sky.static_mesh_component.set_cast_shadow(False)
bg=material('M_MKZ_StudioBackdrop',(.15,.18,.22),0,1);bg.set_editor_property('two_sided',True);bg.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT);L.delete_all_material_expressions(bg);n=L.create_material_expression(bg,u.MaterialExpressionConstant3Vector);n.constant=u.LinearColor(30,36,44,1);L.connect_material_property(n,'',u.MaterialProperty.MP_EMISSIVE_COLOR);L.recompile_material(bg);E.save_loaded_asset(bg,False);sky.static_mesh_component.set_material(0,bg)
for loc,rot,power,sx,sy in [((100,-300,600),(-65,95,0),120000,700,300),((-300,300,400),(-40,-45,0),100000,500,200),((500,100,350),(-35,-170,0),80000,300,400),((-50,-600,300),(-20,90,0),50000,500,150)]:
    light=u.EditorLevelLibrary.spawn_actor_from_class(u.RectLight,u.Vector(*loc),u.Rotator(*rot));c=light.rect_light_component;c.set_intensity(power);c.set_editor_property('source_width',sx);c.set_editor_property('source_height',sy);c.set_editor_property('attenuation_radius',3000);c.set_editor_property('mobility',u.ComponentMobility.MOVABLE)
skylight=u.EditorLevelLibrary.spawn_actor_from_class(u.SkyLight,u.Vector(0,0,300));skylight.light_component.set_editor_property('mobility',u.ComponentMobility.MOVABLE);skylight.light_component.set_intensity(1);skylight.light_component.set_editor_property('sky_distance_threshold',1000)
u.EditorLevelLibrary.save_current_level();report['lab_map']=lab
p=Path('/mnt/simulations/carla/Unreal/CarlaUnreal/Content/Carla/Config/VehicleParameters.json');raw=p.read_bytes();cat=json.loads(raw)
if not any(v['Make'].lower()=='lincoln'and v['Model'].lower()=='mkz_refined'for v in cat['Vehicles']):
    original=dict(next(v for v in cat['Vehicles']if v['Make'].lower()=='lincoln'and v['Model'].lower()=='mkz'));original.update(Make='lincoln',Model='mkz_refined',Class=report['blueprint']+'.BP_LincolnMKZ_Refined_C',RecommendedColors=[{'R':43,'G':60,'B':72,'A':255}],NumberOfWheels=4,Generation=3);cat['Vehicles'].append(original)
    assert p.read_bytes()==raw,'Concurrent catalogue change';tmp=p.with_suffix('.exterior.tmp');tmp.write_text(json.dumps(cat,indent=2)+'\n');tmp.replace(p)
profile=Path('/mnt/simulations/carla/Unreal/CarlaUnreal/Content/Carla/Config/PostProcess/MKZExteriorStudio.json');profile.write_bytes((R/'reference/MKZExteriorStudio.json').read_bytes())
xodr=Path('/mnt/simulations/carla/Unreal/CarlaUnreal/Content/VehicleExteriors/LincolnMKZ/OpenDrive/L_ExteriorStudio.xodr');xodr.parent.mkdir(exist_ok=True);xodr.write_bytes((R/'reference/L_ExteriorStudio.xodr').read_bytes())
report['status']='INSTALLED_PENDING_RUNTIME_VALIDATION';(R/'native-install.json').write_text(json.dumps(report,indent=2));u.log('EXTERIOR_INSTALL_COMPLETE')
