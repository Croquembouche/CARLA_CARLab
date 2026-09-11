from pathlib import Path
import bpy, addon_utils, os
out = Path(os.environ.get('CARLAB_VERIFICATION_DIR', '/mnt/simulations/verification'))
out.mkdir(parents=True, exist_ok=True)
for module in ('rigify', 'send2ue', 'ue2rigify'):
    if not addon_utils.check(module)[1]:
        addon_utils.enable(module, default_set=True, persistent=True)
    assert addon_utils.check(module)[1], module
bpy.ops.wm.save_userpref()
bpy.ops.object.select_all(action='DESELECT')
cube = bpy.data.objects['Cube']
cube.select_set(True)
bpy.context.view_layer.objects.active = cube
from send2ue.core.io import fbx
fbx.export(filepath=str(out/'send2ue-cube.fbx'), use_selection=True, axis_forward='-Y', axis_up='Z', bake_anim=False)
assert (out/'send2ue-cube.fbx').stat().st_size > 1000
bpy.context.scene.render.engine = 'CYCLES'
bpy.context.scene.cycles.samples = 8
bpy.context.scene.render.resolution_x = 256
bpy.context.scene.render.resolution_y = 256
bpy.context.scene.render.resolution_percentage = 100
bpy.context.scene.render.filepath = str(out/'blender-cube.png')
bpy.ops.render.render(write_still=True)
print('BLENDER_ADDONS_FBX_RENDER_VERIFIED', bpy.app.version_string)
