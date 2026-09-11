"""Reproducible Blender cabin workspace and rigged, seated test mannequin.
Stock CARLA geometry/textures remain identifiable; no claim of new OEM CAD.
Blender: meters, +X forward, +Y left. Unreal import: centimeters, +Y right.
"""
import bpy, math, json, sys
from pathlib import Path
from mathutils import Vector
ROOT=Path('/mnt/simulations/vehicle-interiors')
OUT=ROOT/'exports'; OUT.mkdir(exist_ok=True)
RENDER=ROOT/'renders'; RENDER.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'reference/lincoln-stock.blend'))
scene=bpy.context.scene
scene.unit_settings.system='METRIC'
scene.unit_settings.scale_length=1.0
for o in list(scene.objects):
    if o.type=='MESH':
        bpy.context.view_layer.objects.active=o
        o.select_set(True)
        for mod in list(o.modifiers):
            if mod.type=='ARMATURE': bpy.ops.object.modifier_apply(modifier=mod.name)
        world=o.matrix_world.copy(); o.parent=None; o.matrix_world=world
        o.select_set(False)
    elif o.type=='ARMATURE': o.hide_render=True
stock=list(o for o in scene.objects if o.type=='MESH')

def material(name,color,metal=0,rough=.45):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Roughness'].default_value=rough; p.inputs['Metallic'].default_value=metal
    return m

for m in bpy.data.materials:
    if 'Interior' not in m.name: continue
    prefix='T_LincolnMKZ_Interior01' if '01' in m.name else 'T_LincolnMKZ_Interior02'
    m.use_nodes=True; nt=m.node_tree; nt.nodes.clear()
    output=nt.nodes.new('ShaderNodeOutputMaterial'); p=nt.nodes.new('ShaderNodeBsdfPrincipled'); nt.links.new(p.outputs['BSDF'],output.inputs[0])
    def tex(suffix,noncolor=False):
        n=nt.nodes.new('ShaderNodeTexImage'); n.image=bpy.data.images.load(str(ROOT/'reference'/f'{prefix}_{suffix}.tga'),check_existing=True)
        if noncolor: n.image.colorspace_settings.name='Non-Color'
        return n
    d=tex('d'); nt.links.new(d.outputs['Color'],p.inputs['Base Color'])
    n=tex('n',True); normal=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(n.outputs['Color'],normal.inputs['Color']); nt.links.new(normal.outputs[0],p.inputs['Normal'])
    orm=tex('ormh' if '01' in m.name else 'orm',True); sep=nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(orm.outputs['Color'],sep.inputs['Color']); nt.links.new(sep.outputs['Green'],p.inputs['Roughness']); nt.links.new(sep.outputs['Blue'],p.inputs['Metallic'])
bodymat=bpy.data.materials.get('MI_LincolnMKZ_Bodywork')
if bodymat:
    bodymat.use_nodes=True; p=bodymat.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(.1,.15,.19,1); p.inputs['Metallic'].default_value=.75; p.inputs['Roughness'].default_value=.22
# Door meshes are exported about their hinges, positioned using the vehicle skeleton.
audit=json.loads((ROOT/'reference/blender-audit.json').read_text())
hinges={x['name']:x['head'] for x in audit[0]['bones']}
for short,long in [('FL','Front_Left'),('FR','Front_Right'),('RL','Rear_Left'),('RR','Rear_Right')]:
    before=set(scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(ROOT/'reference'/f'SM_LincolnMKZ_Door_{short}.fbx'))
    for o in set(scene.objects)-before:
        if o.type=='MESH':
            if o.name.startswith('UCX_'):
                bpy.data.objects.remove(o,do_unlink=True)
                continue
            o.location+=Vector(hinges['Door_'+long]); stock.append(o)
            for j,m in enumerate(o.data.materials):
                if m:
                    original=bpy.data.materials.get(m.name.split('.')[0])
                    if original: o.data.materials[j]=original

orange=material('M_Mannequin_Shell',(.82,.26,.055),.12,.34)
dark=material('M_Mannequin_Joints',(.045,.055,.065),.55,.28)
white=material('M_Mannequin_Eye',(.86,.89,.9),0,.2)
iris=material('M_Mannequin_Iris',(.025,.08,.105),.1,.23)
rubber=material('M_Interior_Mat',(.018,.022,.026),0,.94)
seam=material('M_Interior_Edge',(.085,.09,.095),0,.65)
parts=[]; weights={}; bones={}
def sphere(name,loc,scale,mat,bone=None):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=20,location=loc)
    o=bpy.context.object; o.name=name; o.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.append(mat)
    for p in o.data.polygons:p.use_smooth=True
    if bone: parts.append(o); weights[o.name]=bone
    return o
def link(name,a,b,radius,mat,bone):
    a,b=Vector(a),Vector(b)
    o=sphere(name,(a+b)/2,(radius,radius,(b-a).length/2),mat,bone)
    o.rotation_mode='QUATERNION'; o.rotation_quaternion=(b-a).to_track_quat('Z','Y'); return o
def bone(name,a,b,parent=None): bones[name]=(Vector(a),Vector(b),parent)
# Flat list of deform bones under root: explicit component-space targets are
# stable when replacing the reference appearance with another research rig.
bone('root',(0,0,0),(0,0,.1))
bone('pelvis',(0,0,0),(0,0,.17),'root')
bone('spine',(0,0,.17),(-.025,0,.44),'root')
bone('head',(-.035,0,.65),(-.035,0,.84),'root')
sphere('Pelvis',(0,0,.09),(.14,.155,.12),orange,'pelvis')
sphere('Torso',(-.025,0,.34),(.13,.205,.235),orange,'spine')
sphere('Waist',(0,0,.17),(.105,.12,.065),dark,'pelvis')
link('Neck',(-.035,0,.53),(-.035,0,.65),.05,dark,'head')
sphere('Head',(-.035,0,.715),(.093,.083,.12),orange,'head')
sphere('Face',(.036,0,.7),(.04,.068,.073),orange,'head')
for side,sign in [('l',1),('r',-1)]:
    shoulder=(-.025,sign*.215,.465); elbow=(.095,sign*.255,.23); hand=(.33,sign*.2,.32)
    bone('upperarm_'+side,shoulder,elbow,'root'); bone('forearm_'+side,elbow,hand,'root'); bone('hand_'+side,hand,(.41,sign*.2,.32),'root')
    for label,loc,rad in [('Shoulder',shoulder,.059),('Elbow',elbow,.042),('Wrist',hand,.031)]: sphere(label+'_'+side,loc,(rad,rad,rad),dark,('upperarm_' if label=='Shoulder' else 'forearm_' if label=='Elbow' else 'hand_')+side)
    link('UpperArm_'+side,shoulder,elbow,.048,orange,'upperarm_'+side)
    link('Forearm_'+side,elbow,hand,.04,orange,'forearm_'+side)
    sphere('Palm_'+side,(hand[0]+.045,hand[1],hand[2]),(.065,.039,.022),orange,'hand_'+side)
    for f in range(4):
        y=hand[1]+(f-1.5)*.017
        link('Finger_'+side+str(f),(hand[0]+.07,y,hand[2]),(hand[0]+.125-abs(f-1.5)*.008,y,hand[2]-.014),.008,orange,'hand_'+side)
    hip=(0,sign*.1,.035); knee=(.395,sign*.13,-.035); ankle=(.50,sign*.13,-.345)
    bone('thigh_'+side,hip,knee,'root'); bone('shin_'+side,knee,ankle,'root'); bone('foot_'+side,ankle,(.66,sign*.13,-.36),'root')
    link('Thigh_'+side,hip,knee,.077,orange,'thigh_'+side); sphere('Knee_'+side,knee,(.055,.055,.055),dark,'shin_'+side)
    link('Shin_'+side,knee,ankle,.052,orange,'shin_'+side); sphere('Foot_'+side,(.57,sign*.13,-.36),(.115,.049,.035),dark,'foot_'+side)
    eye=(.052,sign*.033,.747)
    bone('eye_'+side,eye,(.077,eye[1],eye[2]),'head'); bone('lid_'+side,(.055,eye[1],eye[2]),(.08,eye[1],eye[2]),'head')
    sphere('Eye_'+side,eye,(.022,.019,.015),white,'eye_'+side)
    sphere('Iris_'+side,(.072,eye[1],eye[2]),(.004,.008,.009),iris,'eye_'+side)
    lid=sphere('Eyelid_'+side,(.058,eye[1],eye[2]),(.022,.020,.016),orange,'lid_'+side)

bpy.ops.object.select_all(action='DESELECT')
for o in parts:
    o.select_set(True)
    vg=o.vertex_groups.new(name=weights[o.name]); vg.add(list(range(len(o.data.vertices))),1.0,'REPLACE')
bpy.context.view_layer.objects.active=parts[0]
bpy.ops.object.join(); mannequin=bpy.context.object; mannequin.name='SK_SeatedMannequin'
# Apply origin so rest skeleton, mesh, and controller all use the same seat frame.
scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
for v in mannequin.data.vertices:v.co*=.87
armdata=bpy.data.armatures.new('SeatedMannequinRig'); arm=bpy.data.objects.new('Armature',armdata); scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active=arm; mannequin.select_set(False); arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
for name,(a,b,parent) in bones.items():
    e=armdata.edit_bones.new(name); e.head=a*.87; e.tail=b*.87
    if parent:e.parent=armdata.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')
mod=mannequin.modifiers.new('Armature','ARMATURE'); mod.object=arm; mannequin.parent=arm
mannequin.select_set(True)
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_SeatedMannequin.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,bake_anim=False,axis_forward='-Y',axis_up='Z')

# Individually selectable floor mats and stitched edging are new cabin detail.
details=[]
def rounded(name,loc,size,mat):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc); o=bpy.context.object; o.name=name; o.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.data.materials.append(mat); bevel=o.modifiers.new('Soft edge','BEVEL'); bevel.width=.008; bevel.segments=3
    bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=bevel.name)
    details.append(o); return o
for x,z,length in [(.80,.350,.38),(-.40,.394,.30)]:
    for y in [-.38,.38]:
        rounded('FloorMat',(x,y,z),(length,.34,.009),rubber)
        for yy in [-.155,.155]: rounded('MatBinding',(x,y+yy,z+.006),(length-.025,.006,.003),seam)
        for xx in [-(length/2-.014),length/2-.014]: rounded('MatBinding',(x+xx,y,z+.006),(.006,.31,.003),seam)
bpy.ops.object.select_all(action='DESELECT')
for o in details:o.select_set(True)
bpy.context.view_layer.objects.active=details[0]; bpy.ops.object.join(); detail=bpy.context.object; detail.name='SM_LincolnCabinDetails'
scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.export_scene.fbx(filepath=str(OUT/'SM_LincolnCabinDetails.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')

seats={'driver':[.18,.40,.69],'front_passenger':[.18,-.40,.69],'rear_left':[-.84,.40,.69],'rear_center':[-.84,0,.69],'rear_right':[-.84,-.40,.69]}
for name,loc in seats.items():
    o=bpy.data.objects.new('Seat_'+name,None); scene.collection.objects.link(o); o.location=loc; o.empty_display_size=.08
arm.location=seats['driver']
for side in ['l','r']:arm.pose.bones['lid_'+side].scale=(.001,.001,.001)
for name,value in [('blink',0.0),('look_yaw',0.0),('look_pitch',0.0)]:arm[name]=value
for prop in ['blink','look_yaw','look_pitch']:
    arm.id_properties_ui(prop).update(min=0 if prop=='blink' else -60,max=1 if prop=='blink' else 60)
def driver(pose_bone,channel,index,prop,expression):
    d=pose_bone.driver_add(channel,index).driver;d.type='SCRIPTED'
    v=d.variables.new();v.name='value';v.type='SINGLE_PROP';v.targets[0].id=arm;v.targets[0].data_path='["'+prop+'"]';d.expression=expression
for side in ['l','r']:
    for i in range(3):driver(arm.pose.bones['lid_'+side],'scale',i,'blink','max(0.001,value)')
arm.pose.bones['head'].rotation_mode='XYZ'
driver(arm.pose.bones['head'],'rotation_euler',0,'look_pitch','value*pi/180')
driver(arm.pose.bones['head'],'rotation_euler',1,'look_yaw','value*pi/180')
arm['description']='Seat-local research rig. Native UE component supplies head/eye targets, blink and arm IK. Quest 2 does not track eyes.'
(ROOT/'seat-layout.json').write_text(json.dumps({'blender_meters':seats,'unreal_cm':{k:[v[0]*100,-v[1]*100,v[2]*100] for k,v in seats.items()}},indent=2))
def camera(name,loc,target,lens=22):
    bpy.ops.object.camera_add(location=loc); o=bpy.context.object; o.name=name; o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler(); o.data.lens=lens; o.data.clip_start=.015; return o
cameras=[camera('Camera_Cabin',(-1.02,-.1,1.25),(.45,0,.8),18),camera('Camera_Passenger',(.53,-.52,1.27),(.16,.4,1.03),20),camera('Camera_Driver',(.23,.4,1.30),(1.3,.22,.92),20)]
world=bpy.data.worlds.new('StudioSky'); scene.world=world; world.use_nodes=True; world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.75,.9,1); world.node_tree.nodes['Background'].inputs[1].default_value=.5
for name,loc,energy,size in [('CabinSoftbox',(0,0,1.42),12,1.4),('WindshieldSoftbox',(1.4,0,1.4),35,1.3)]:
    bpy.ops.object.light_add(type='AREA',location=loc); l=bpy.context.object; l.name=name; l.data.energy=energy; l.data.shape='DISK'; l.data.size=size
    l.rotation_euler=(Vector((-.15,0,.65))-l.location).to_track_quat('-Z','Y').to_euler()
scene.render.engine='CYCLES'; scene.cycles.samples=32; scene.cycles.use_denoising=True
scene.render.resolution_x=1280; scene.render.resolution_y=800; scene.render.resolution_percentage=100
scene.camera=cameras[0]
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Lincoln_Interior_Mannequin.blend'))
for cam in ([] if '--no-render' in sys.argv else cameras[:2]):
    scene.camera=cam; scene.render.filepath=str(RENDER/(cam.name+'.png')); bpy.ops.render.render(write_still=True)
print('LINCOLN_BLENDER_COMPLETE')
