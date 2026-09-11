import bpy,json,math
from pathlib import Path
ROOT=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Lincoln_Interior_Mannequin.blend'))
arm=bpy.data.objects['Armature'];scene=bpy.context.scene
report={'checks':[],'headset_tested':False}
for blink in (0,1,0):
    arm['blink']=blink;arm.update_tag();bpy.context.view_layer.update()
    vals=list(arm.pose.bones['lid_l'].scale)
    assert all(abs(v-max(.001,blink))<.01 for v in vals),vals
    report['checks'].append({'blink_input':blink,'lid_scale':vals})
arm['look_yaw']=30;arm.update_tag();bpy.context.view_layer.update()
assert abs(arm.pose.bones['head'].rotation_euler.y-math.radians(30))<.001
report['checks'].append({'look_yaw':30,'bone_radians':arm.pose.bones['head'].rotation_euler.y})
arm['look_yaw']=0;arm.update_tag()
layout=json.loads((ROOT/'seat-layout.json').read_text())['blender_meters']
for name,loc in layout.items():
    arm.location=loc;bpy.context.view_layer.update()
    head=arm.matrix_world@arm.pose.bones['head'].head
    assert .9<head.z<1.5
    assert abs(head.y)<.7
    report['checks'].append({'seat':name,'head_position':list(head)})
arm.location=layout['rear_left'];scene.camera=bpy.data.objects['Camera_Passenger']
scene.camera.location=(.4,-.15,1.30)
from mathutils import Vector
scene.camera.rotation_euler=(Vector((-.9,.4,1.03))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=960;scene.render.resolution_y=600;scene.cycles.samples=24
scene.render.filepath=str(ROOT/'renders/Camera_Rear_Occupant.png');bpy.ops.render.render(write_still=True)
report['result']='PASS'
(ROOT/'blender-verification.json').write_text(json.dumps(report,indent=2))
print('BLENDER_POSE_CHECKS_PASS')
