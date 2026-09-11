import bpy,sys
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
s=bpy.context.scene;nt=s.world.node_tree;bg=nt.nodes.get('Background');env=nt.nodes.new('ShaderNodeTexEnvironment');env.image=bpy.data.images.load('/mnt/simulations/blender-5.2.1-linux-x64/5.2/datafiles/studiolights/world/forest.exr');nt.links.new(env.outputs['Color'],bg.inputs[0]);bg.inputs[1].default_value=.45
for o in s.objects:
 if o.type=='LIGHT' and o.name in ['Window_Key','Windshield_Daylight','Rear_Window']:o.data.energy={'Window_Key':60.5,'Windshield_Daylight':99,'Rear_Window':44}[o.name]
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
optix=[d for d in prefs.devices if d.type=='OPTIX']
for d in prefs.devices:d.use=bool(optix) and d==optix[min(1,len(optix)-1)]
s.cycles.device='GPU';s.camera=bpy.data.objects['MKZ_Dashboard']
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'))
for name in (['MKZ_Rear_Seats'] if '--rear-only' in sys.argv else ['MKZ_Dashboard','MKZ_Front_Seats','MKZ_Rear_Seats']):
 s.camera=bpy.data.objects[name];s.render.filepath=str(R/'renders'/(name+'_Daylight.png'));bpy.ops.render.render(write_still=True)
