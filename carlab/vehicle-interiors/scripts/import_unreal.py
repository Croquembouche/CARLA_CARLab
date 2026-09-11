"""Import local authored assets, create a Lincoln variant, register in CARLA.
Run only after compiling InteriorOccupantComponent. Re-runnable; stock mesh untouched.
"""
import json, shutil
from pathlib import Path
import unreal as u
ROOT=Path('/mnt/simulations/vehicle-interiors')
DEST='/Game/VehicleInteriors/Lincoln'
u.EditorAssetLibrary.make_directory(DEST)
report={}

def import_fbx(name,skeletal=False):
    options=u.FbxImportUI()
    options.import_mesh=True; options.import_materials=False; options.import_textures=False
    options.import_as_skeletal=skeletal; options.import_animations=False
    options.automated_import_should_detect_type=False
    options.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else u.FBXImportType.FBXIT_STATIC_MESH
    data=options.skeletal_mesh_import_data if skeletal else options.static_mesh_import_data
    data.convert_scene=True; data.convert_scene_unit=True; data.force_front_x_axis=False
    if skeletal:
        options.create_physics_asset=False
    else:
        options.static_mesh_import_data.combine_meshes=True
        options.static_mesh_import_data.auto_generate_collision=False
    task=u.AssetImportTask(); task.filename=str(ROOT/'exports'/(name+'.fbx'));task.destination_path=DEST
    task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=True;task.options=options
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset=u.load_asset(DEST+'/'+name)
    if not asset:raise RuntimeError('Import failed: '+name)
    return asset

mesh=import_fbx('SK_SeatedMannequin',True)
detail=import_fbx('SM_LincolnCabinDetails')
colors={'M_Mannequin_Shell':((.82,.26,.055),.12,.34),'M_Mannequin_Joints':((.045,.055,.065),.55,.28),'M_Mannequin_Eye':((.86,.89,.9),0,.2),'M_Mannequin_Iris':((.025,.08,.105),.1,.23),'M_Interior_Mat':((.018,.022,.026),0,.94),'M_Interior_Edge':((.085,.09,.095),0,.65)}
materials={}
for name,(color,metal,rough) in colors.items():
    m=u.load_asset(DEST+'/'+name)
    if not m:
        m=u.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,u.Material,u.MaterialFactoryNew())
        c=u.MaterialEditingLibrary.create_material_expression(m,u.MaterialExpressionConstant3Vector)
        c.constant=u.LinearColor(*color,1)
        u.MaterialEditingLibrary.connect_material_property(c,'',u.MaterialProperty.MP_BASE_COLOR)
        for prop,value in [(u.MaterialProperty.MP_METALLIC,metal),(u.MaterialProperty.MP_ROUGHNESS,rough)]:
            node=u.MaterialEditingLibrary.create_material_expression(m,u.MaterialExpressionConstant); node.r=value
            u.MaterialEditingLibrary.connect_material_property(node,'',prop)
        u.MaterialEditingLibrary.recompile_material(m)
    if name.startswith('M_Mannequin'):
        m.set_editor_property('used_with_skeletal_mesh',True)
        u.MaterialEditingLibrary.recompile_material(m)
    materials[name]=m
    u.EditorAssetLibrary.save_loaded_asset(m)
slots=list(mesh.materials)
for i,slot in enumerate(slots):
    name=str(slot.material_slot_name)
    if name in materials: slot.material_interface=materials[name]
mesh.set_editor_property('materials',slots)
for i,slot in enumerate(detail.static_materials):
    name=str(slot.material_slot_name)
    if name in materials: detail.set_material(i,materials[name])
u.EditorAssetLibrary.save_loaded_asset(mesh);u.EditorAssetLibrary.save_loaded_asset(detail)

bp_path=DEST+'/BP_LincolnMKZ_Interior'
bp=u.load_asset(bp_path)
if not bp:bp=u.EditorAssetLibrary.duplicate_asset('/Game/Carla/Blueprints/Vehicles/LincolnMKZ/BP_LincolnMKZ',bp_path)
sub=u.get_engine_subsystem(u.SubobjectDataSubsystem)
lib=u.SubobjectDataBlueprintFunctionLibrary
handles=sub.k2_gather_subobject_data_for_blueprint(bp)
objects={}
for handle in handles:
    data=lib.get_data(handle);obj=lib.get_object(data)
    objects[obj.get_name().replace('_GEN_VARIABLE','')]=(handle,obj)
root_handle=objects['VehicleMesh'][0]
def add(name,cls,parent=root_handle):
    if name in objects:return objects[name]
    handle,reason=sub.add_new_subobject(u.AddNewSubobjectParams(parent_handle=parent,new_class=cls,blueprint_context=bp))
    if not lib.is_handle_valid(handle):raise RuntimeError(str(reason))
    sub.rename_subobject(handle,u.Text(name))
    obj=lib.get_object(lib.get_data(handle)); objects[name]=(handle,obj)
    return handle,obj
layout=json.loads((ROOT/'seat-layout.json').read_text())['unreal_cm']
_,d=add('CabinDetails',u.StaticMeshComponent);d.set_static_mesh(detail);d.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
h,occupant=add('InteriorOccupant',u.InteriorOccupantComponent)
occupant.set_editor_property('mannequin_mesh',mesh)
occupant.set_editor_property('seat_locations',[u.Vector(*v) for v in layout.values()])
occupant.set_editor_property('relative_location',u.Vector(*layout['driver']))
occupant.set_editor_property('enable_desktop_controls',False)
_,preview=add('MannequinPreview',u.SkeletalMeshComponent,h)
preview.set_skeletal_mesh_asset(mesh);preview.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);preview.set_hidden_in_game(True)
for name,loc in layout.items():
    _,seat=add('Seat_'+name,u.SceneComponent);seat.set_editor_property('relative_location',u.Vector(*loc))
u.BlueprintEditorLibrary.compile_blueprint(bp)
u.EditorAssetLibrary.save_loaded_asset(bp)

# The installed UE5 native factory loads JSON, not the old BP Vehicles array.
source=Path('/mnt/simulations/carla/Unreal/CarlaUnreal/Content/Carla/Config/VehicleParameters.json')
catalog=json.loads(source.read_text());vehicles=catalog['Vehicles']
report['stock_lincoln_entries']=[{'make':v['Make'],'model':v['Model']} for v in vehicles if 'lincoln' in v['Make'].lower()]
if not any(v['Make'].lower()=='lincoln' and v['Model'].lower()=='mkz_interior' for v in vehicles):
    backup=ROOT/'backups/VehicleParameters.json';backup.parent.mkdir(exist_ok=True)
    if not backup.exists():shutil.copy2(source,backup)
    v=dict(next(v for v in vehicles if 'lincoln' in v['Make'].lower()))
    v.update(Make='lincoln',Model='mkz_interior',Class=bp_path+'.BP_LincolnMKZ_Interior_C',NumberOfWheels=4,Generation=3,SupportedDrivers=[])
    vehicles.append(v)
    temporary=source.with_suffix('.interior.tmp');temporary.write_text(json.dumps(catalog,indent='\t')+'\n');temporary.replace(source)
report['blueprint']=bp_path;report['carla_id']='vehicle.lincoln.mkz_interior';report['seat_locations']=layout
# Check the actual FBX axis conversion before runtime pose validation.
preview_actor=u.EditorLevelLibrary.spawn_actor_from_class(u.Actor,u.Vector())
component=u.new_object(u.PoseableMeshComponent,outer=preview_actor)
component.set_skeletal_mesh(mesh)
report['imported_bone_names']=[str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
u.EditorLevelLibrary.destroy_actor(preview_actor)
# A directly openable stationary lab; controls are enabled on this one instance.
lab=DEST+'/L_LincolnInteriorLab'
if not u.EditorAssetLibrary.does_asset_exist(lab) or not (ROOT/'import-report.json').exists():
    u.EditorLevelLibrary.new_level(lab)
    car=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector(0,0,8))
    car.set_actor_label('Lincoln Interior Test Vehicle')
    c=car.get_component_by_class(u.InteriorOccupantComponent)
    c.set_editor_property('enable_desktop_controls',True)
    floor=u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-10))
    floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(u.Vector(20,20,.2))
    sun=u.EditorLevelLibrary.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,300),u.Rotator(-50,35,0))
    sun.light_component.set_intensity(3)
    for loc in [(120,0,170),(-70,0,180)]:
        light=u.EditorLevelLibrary.spawn_actor_from_class(u.PointLight,u.Vector(*loc))
        light.point_light_component.set_intensity(1800)
        light.point_light_component.set_attenuation_radius(600)
    cam=u.EditorLevelLibrary.spawn_actor_from_class(u.CameraActor,u.Vector(53,52,135),u.Rotator(-13,-112,0))
    cam.set_editor_property('auto_activate_for_player',u.AutoReceiveInput.PLAYER0)
    cam.get_component_by_class(u.CameraComponent).set_field_of_view(90)
    u.EditorLevelLibrary.save_current_level()
report['lab_map']=lab
(ROOT/'import-report.json').write_text(json.dumps(report,indent=2))
u.log('LINCOLN_IMPORT_COMPLETE')
