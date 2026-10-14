import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');D='/Game/VehicleInteriors/Lincoln';a=u.AssetToolsHelpers.get_asset_tools()
source=(R/'scripts/import_completion.py').read_text();exec(compile(source[:source.index('meshes={}')],'material-helpers','exec'))
for name,c,rough,metal in [('MKZ_Vent_Recess',(.003,.004,.005),.72,0),('MKZ_Vent_Louver',(.014,.017,.021),.38,0),('MKZ_Fine_Satin',(.22,.235,.25),.43,.85),('MKZ_Seam_Shadow',(.0025,.003,.0035),.78,0)]:material(name,c,rough,metal)
source=(R/'scripts/import_brochure_cabin.py').read_text();m=u.load_asset(D+'/MI_MKZ_Brochure_Interior');assert m
exec(compile(source[source.index("name='SM_MKZ_Cabin_Brochure'"):],'import-cabin','exec'))
