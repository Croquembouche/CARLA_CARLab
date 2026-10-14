import bpy,json,math,os
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');S=bpy.context.scene
bpy.ops.wm.read_factory_settings(use_empty=True);S=bpy.context.scene
S.unit_settings.system='METRIC';S.unit_settings.scale_length=1
source=R/'reference/SK_LincolnMKZ.fbx'
if not source.exists():source=Path('/mnt/simulations/vehicle-interiors/reference/SK_LincolnMKZ.fbx')
bpy.ops.import_scene.fbx(filepath=str(source))
arm=next(o for o in S.objects if o.type=='ARMATURE')
heads={b.name:list(arm.matrix_world@b.head_local)for b in arm.data.bones}
body=next(o for o in S.objects if o.type=='MESH' and not o.name.startswith('UCX'))
body.name='StockBody'
for o in list(S.objects):
    if o.type=='MESH':
        bpy.context.view_layer.objects.active=o
        for mod in list(o.modifiers):
            if mod.type=='ARMATURE':bpy.ops.object.modifier_apply(modifier=mod.name)
        mw=o.matrix_world.copy();o.parent=None;o.matrix_world=mw
    elif o.type=='ARMATURE':o.hide_render=True
for name in ['SM_LincolnMKZ_Emissive','SM_LincolnMKZ_Glass_Ext1']+[f'SM_LincolnMKZ_Door_{c}{tail}'for c in ['FL','FR','RL','RR']for tail in ['','_Glass_Ext1']]:
    before=set(S.objects);bpy.ops.import_scene.fbx(filepath=str(R/'reference'/(name+'.fbx')))
    for o in set(S.objects)-before:
        if o.name.startswith(('UCX_','UBX_','UCP_','USP_')):bpy.data.objects.remove(o,do_unlink=True);continue
        if o.type=='MESH':
            o.name=name
            if '_Door_' in name and '_Glass_'not in name:
                c=name.split('_')[-1];bn={'FL':'Front_Left','FR':'Front_Right','RL':'Rear_Left','RR':'Rear_Right'}[c];o.location+=Vector(heads['Door_'+bn])
            for i,m in enumerate(o.data.materials):
                if m:o.data.materials[i]=bpy.data.materials.get(m.name.split('.')[0]) or m
def principled(m):
    m.use_nodes=True;nt=m.node_tree;nt.nodes.clear();p=nt.nodes.new('ShaderNodeBsdfPrincipled');out=nt.nodes.new('ShaderNodeOutputMaterial');nt.links.new(p.outputs['BSDF'],out.inputs['Surface']);return p
for m in bpy.data.materials:
    p=principled(m);nt=m.node_tree
    if 'Interior' in m.name:
        prefix='T_LincolnMKZ_Interior01' if '01'in m.name else 'T_LincolnMKZ_Interior02'
        def tex(suffix,noncolor=False):
            n=nt.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(R/'reference'/f'{prefix}_{suffix}.tga'),check_existing=True)
            if noncolor:n.image.colorspace_settings.name='Non-Color'
            return n
        d=tex('d');nt.links.new(d.outputs['Color'],p.inputs['Base Color']);n=tex('n',True);nm=nt.nodes.new('ShaderNodeNormalMap');nt.links.new(n.outputs['Color'],nm.inputs['Color']);nt.links.new(nm.outputs['Normal'],p.inputs['Normal'])
        orm=tex('ormh'if '01'in m.name else 'orm',True);sp=nt.nodes.new('ShaderNodeSeparateColor');nt.links.new(orm.outputs['Color'],sp.inputs['Color']);nt.links.new(sp.outputs['Green'],p.inputs['Roughness']);nt.links.new(sp.outputs['Blue'],p.inputs['Metallic'])
    elif 'Bodywork'in m.name:
        p.inputs['Base Color'].default_value=(.024,.045,.065,1);p.inputs['Metallic'].default_value=.65;p.inputs['Roughness'].default_value=.30;p.inputs['Coat Weight'].default_value=.6;p.inputs['Coat Roughness'].default_value=.12
    elif 'Glass'in m.name:
        if 'Emissive'in m.name:
            p.inputs['Base Color'].default_value=(.72,.74,.77,1);p.inputs['Roughness'].default_value=.08;p.inputs['Transmission Weight'].default_value=1;p.inputs['IOR'].default_value=1.48
        else:
            p.inputs['Base Color'].default_value=(.32,.39,.43,1);p.inputs['Roughness'].default_value=.05;p.inputs['Transmission Weight'].default_value=1;p.inputs['IOR'].default_value=1.48
    elif 'Emissive'in m.name:
        p.inputs['Base Color'].default_value=(.15,.16,.17,1);p.inputs['Metallic'].default_value=.65;p.inputs['Roughness'].default_value=.22
S.render.engine='CYCLES';S.cycles.samples=64;S.cycles.use_denoising=True
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='OPTIX'
S.cycles.device='GPU';S.render.resolution_x=1600;S.render.resolution_y=1000;S.render.resolution_percentage=100
S.view_settings.view_transform='AgX';S.view_settings.look='AgX - Medium High Contrast';S.view_settings.exposure=0
world=bpy.data.worlds.new('StudioWorld');S.world=world;world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.16,.19,.23,1);world.node_tree.nodes['Background'].inputs[1].default_value=.55
def mat(name,c,rough,metal=0):
    m=bpy.data.materials.new(name);p=principled(m);p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;return m
bpy.ops.mesh.primitive_plane_add(size=200);floor=bpy.context.object;floor.name='StudioFloor';floor.location.z=-.006;floor.data.materials.append(mat('StudioFloorMaterial',(.105,.12,.135),.3))
for name,loc,power,sizex,sizey in [('Key',(1,3,6),1800,7,3),('Rim',(-3,-3,4),1400,5,2),('Front',(5,-1,3.5),1000,3,4),('Side',(-.5,6,3),700,5,1.5)]:
    bpy.ops.object.light_add(type='AREA',location=loc);l=bpy.context.object;l.name='Studio'+name;l.data.energy=power;l.data.shape='RECTANGLE';l.data.size=sizex;l.data.size_y=sizey;l.rotation_euler=(Vector((0,0,.8))-l.location).to_track_quat('-Z','Y').to_euler()
views={'front-three-quarter':((6.3,5.4,2.7),(0,0,.8),52),'rear-three-quarter':((-6.3,5.2,2.5),(0,0,.8),52),'front-detail':((4.4,1.8,1.35),(1.95,0,.65),64),'wheel-detail':((2.55,2.8,.9),(1.47,.8,.37),68)}
for name,(loc,target,lens)in views.items():
    bpy.ops.object.camera_add(location=loc);c=bpy.context.object;c.name='Camera_'+name;c.data.lens=lens;c.rotation_euler=(Vector(target)-c.location).to_track_quat('-Z','Y').to_euler()
bpy.context.view_layer.update()
audit={'bones':heads,'objects':[]}
for o in S.objects:
    if o.type=='MESH'and not o.name.startswith('Studio'):
        vv=[o.matrix_world@v.co for v in o.data.vertices];audit['objects'].append({'name':o.name,'vertices':len(vv),'faces':len(o.data.polygons),'bounds':[[min(v[i]for v in vv),max(v[i]for v in vv)]for i in range(3)],'materials':[m.name for m in o.data.materials]})
(R/'reference/assembly-audit.json').write_text(json.dumps(audit,indent=2))
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Baseline.blend'))
for name in views:
    S.camera=bpy.data.objects['Camera_'+name];S.render.filepath=str(R/'renders'/('baseline-'+name+'.png'));bpy.ops.render.render(write_still=True)
