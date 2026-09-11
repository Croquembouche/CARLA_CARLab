import unreal as u,json
from pathlib import Path
names=['M_Hidden_StockCabin','M_Mannequin_Shell','M_Mannequin_Joints','M_Mannequin_Eye','M_Mannequin_Iris']
report={}
for name in names:
 m=u.load_asset('/Game/VehicleInteriors/Lincoln/'+name)
 m.set_editor_property('used_with_skeletal_mesh',True)
 u.MaterialEditingLibrary.recompile_material(m)
 assert u.EditorAssetLibrary.save_loaded_asset(m,False),name
 report[name]=bool(m.get_editor_property('used_with_skeletal_mesh'))
Path('/mnt/simulations/vehicle-interiors/material-usage-verification.json').write_text(json.dumps(report,indent=2))
