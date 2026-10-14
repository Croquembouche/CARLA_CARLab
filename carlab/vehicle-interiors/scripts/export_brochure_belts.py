import bpy
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));col=bpy.data.collections['Interior_Completion']
for name,worn in [('SM_MKZ_Completion_Details',False),('SM_MKZ_Driver_Worn_Belt',True)]:
 obs=[o for o in col.objects if ('_Worn_' in o.name)==worn];assert obs
 bpy.ops.object.select_all(action='DESELECT')
 for o in obs:o.select_set(True)
 bpy.context.view_layer.objects.active=obs[0];bpy.ops.export_scene.fbx(filepath=str(R/'exports'/(name+'.fbx')),use_selection=True,object_types={'MESH','OTHER'},bake_anim=False,axis_forward='-Y',axis_up='Z')
