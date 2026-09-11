from pathlib import Path
import bpy
prefs=bpy.context.preferences.addons['cycles'].preferences
prefs.compute_device_type='OPTIX'
prefs.get_devices()
selected=False
for device in prefs.devices:
    device.use=(device.type=='OPTIX' and not selected)
    if device.use: selected=True
assert selected
scene=bpy.context.scene
scene.render.engine='CYCLES'
scene.cycles.device='GPU'
scene.cycles.samples=8
scene.render.resolution_x=256
scene.render.resolution_y=256
scene.render.resolution_percentage=100
scene.render.filepath='/mnt/simulations/verification/blender-optix.png'
bpy.ops.render.render(write_still=True)
assert Path(scene.render.filepath).stat().st_size>1000
print('BLENDER_OPTIX_RENDER_VERIFIED')
