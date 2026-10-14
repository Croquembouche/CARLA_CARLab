"""Compile and validate the accepted interior without rebuilding its source materials."""
import unreal as u
import json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors'); D='/Game/VehicleInteriors/Lincoln/'
bp=u.load_asset(D+'BP_LincolnMKZ_Interior');assert bp
sub=u.get_engine_subsystem(u.SubobjectDataSubsystem);lib=u.SubobjectDataBlueprintFunctionLibrary
components={}
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
 data=lib.get_data(h);o=lib.get_object_for_blueprint(data,bp) or lib.get_object(data)
 if isinstance(o,u.StaticMeshComponent):
  mesh=o.get_editor_property('static_mesh')
  components[o.get_name().replace('_GEN_VARIABLE','')]={'mesh':mesh.get_path_name() if mesh else None,'materials':[m.get_path_name() if m else None for m in o.get_materials()]}
assert components['CabinReplacement']['mesh']==D+'SM_MKZ_Cabin_Brochure.SM_MKZ_Cabin_Brochure'
for code in ['FL','FR']:assert components['BrochureDoorDetails_'+code]['mesh']==D+'SM_MKZ_Door_Details_'+code+'.SM_MKZ_Door_Details_'+code
for name in ['MKZ_Ebony_Leather','MKZ_Perforated_Leather','MKZ_Fine_Satin','MI_MKZ_Brochure_Interior','SM_MKZ_Moonroof_Glass','SM_MKZ_Driver_Worn_Belt']:
 assert u.load_asset(D+name),name
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
(R/'accepted-build-native.json').write_text(json.dumps({'result':'COMPILED_AND_SAVED','blueprint':bp.get_path_name(),'vehicle_id':'vehicle.lincoln.mkz_interior','components':components,'user_accepted_revision':'brochure material refinement v3','fresh_runtime_verification':'PENDING'},indent=2))
