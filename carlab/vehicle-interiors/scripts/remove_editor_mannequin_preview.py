import unreal as u,json
bp=u.load_asset('/Game/VehicleInteriors/Lincoln/BP_LincolnMKZ_Interior');sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
handles=sub.k2_gather_subobject_data_for_blueprint(bp);deleted=0
for h in handles:
 obj=lib.get_object(lib.get_data(h))
 if obj.get_name().replace('_GEN_VARIABLE','')=='MannequinPreview':deleted+=sub.delete_subobject(handles[0],h,bp)
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
a=u.EditorLevelLibrary.spawn_actor_from_class(bp.generated_class(),u.Vector());names=[c.get_name() for c in a.get_components_by_class(u.SkeletalMeshComponent)];assert names==['VehicleMesh'],names;u.EditorLevelLibrary.destroy_actor(a)
open('/mnt/simulations/vehicle-interiors/preview-cleanup.json','w').write(json.dumps({'deleted':deleted,'skeletal_components':names,'runtime_mannequin':'Preserved: InteriorOccupant creates a poseable mesh during BeginPlay'},indent=2))
