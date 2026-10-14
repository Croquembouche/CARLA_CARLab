"""Authored material and hardware pass on the stock exterior assembly."""
import bpy,math,json,numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz')
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Baseline.blend'));S=bpy.context.scene
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='OPTIX'
S.cycles.device='GPU'
body=bpy.data.objects['StockBody'];tree=BVHTree.FromObject(body,bpy.context.evaluated_depsgraph_get());inv=body.matrix_world.inverted()
def ray(origin,direction):
    p,n,i,d=tree.ray_cast(inv@Vector(origin),(inv.to_3x3()@Vector(direction)).normalized())
    return body.matrix_world@p if p is not None else None
def mat(name,col,metal,rough,coat=0):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*col,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough;p.inputs['Coat Weight'].default_value=coat;return m
steel=mat('M_MKZ_Fastener',(.36,.39,.42),1,.28)
rubber=mat('M_MKZ_ValveRubber',(.009,.012,.015),0,.68)
lens=mat('M_MKZ_OpticalLens',(.15,.18,.2),.05,.08,.95)
rotor=mat('M_MKZ_BrakeSteel',(.24,.25,.26),1,.32)
for m in bpy.data.materials:
    if 'Bodywork'in m.name:
        p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.024,.045,.065,1);p.inputs['Metallic'].default_value=.5;p.inputs['Roughness'].default_value=.19;p.inputs['Coat Weight'].default_value=1;p.inputs['Coat Roughness'].default_value=.075
        nt=m.node_tree;n=nt.nodes.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=6500;n.inputs['Detail'].default_value=2;tc=nt.nodes.new('ShaderNodeTexCoord');nt.links.new(tc.outputs['Object'],n.inputs['Vector']);b=nt.nodes.new('ShaderNodeBump');b.inputs['Strength'].default_value=.08;b.inputs['Distance'].default_value=.000012;nt.links.new(n.outputs['Fac'],b.inputs['Height']);nt.links.new(b.outputs['Normal'],p.inputs['Normal'])
    elif 'Glass'in m.name and 'Emissive'not in m.name:
        p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.19,.24,.28,1);p.inputs['Roughness'].default_value=.025;p.inputs['IOR'].default_value=1.52
        nt=m.node_tree;vol=nt.nodes.new('ShaderNodeVolumeAbsorption');vol.inputs['Color'].default_value=(.42,.52,.6,1);vol.inputs['Density'].default_value=85;nt.links.new(vol.outputs['Volume'],next(n for n in nt.nodes if n.type=='OUTPUT_MATERIAL').inputs['Volume'])
        for o in S.objects:
            if o.type=='MESH'and m in list(o.data.materials):
                mod=o.modifiers.new('Laminated glass thickness','SOLIDIFY');mod.thickness=.0028;mod.offset=0
# A new atlas is confined to exterior wheel/caliper UV regions. Original files stay intact.
mi=bpy.data.materials['MI_LincolnMKZ_Interior2'];nodes=mi.node_tree.nodes
albedo=next(n.image for n in nodes if n.type=='TEX_IMAGE'and n.image.name.startswith('T_LincolnMKZ_Interior02_d'))
orm=next(n.image for n in nodes if n.type=='TEX_IMAGE'and '_orm' in n.image.name)
w,h=albedo.size;arr=np.empty(w*h*4,np.float32);albedo.pixels.foreach_get(arr);arr=arr.reshape(h,w,4)
ow,oh=orm.size;oa=np.empty(ow*oh*4,np.float32);orm.pixels.foreach_get(oa);oa=oa.reshape(oh,ow,4)
rim_mask=np.zeros((h,w),bool);caliper_mask=np.zeros((h,w),bool)
uv=body.data.uv_layers.active.data;heads=json.loads((R/'reference/assembly-audit.json').read_text())['bones']
wheelheads={n:Vector(v)for n,v in heads.items()if n.startswith('Wheel_')}
def triangle(mask,points):
    pts=np.array(points)*np.array([w-1,h-1]);xmin=max(0,int(np.floor(pts[:,0].min())));xmax=min(w-1,int(np.ceil(pts[:,0].max())));ymin=max(0,int(np.floor(pts[:,1].min())));ymax=min(h-1,int(np.ceil(pts[:,1].max())))
    if xmin>xmax or ymin>ymax:return
    yy,xx=np.mgrid[ymin:ymax+1,xmin:xmax+1];sg=[]
    for a,b in zip(pts,np.roll(pts,-1,axis=0)):sg.append((xx-a[0])*(b[1]-a[1])-(yy-a[1])*(b[0]-a[0]))
    inside=(np.minimum.reduce(sg)>=-.1)|(np.maximum.reduce(sg)<=.1);mask[ymin:ymax+1,xmin:xmax+1]|=inside
selected={'rim':0,'caliper':0}
for p in body.data.polygons:
    if 'Interior2'not in body.data.materials[p.material_index].name:continue
    c=body.matrix_world@p.center
    if not any((c.x-v.x)**2+(c.z-v.z)**2<.24**2 and abs(c.y)>.68 for v in wheelheads.values()):continue
    points=[list(uv[i].uv)for i in p.loop_indices];mean=np.mean(points,axis=0);px=arr[int(np.clip(mean[1]*(h-1),0,h-1)),int(np.clip(mean[0]*(w-1),0,w-1))]
    red=px[0]>.09 and px[0]>px[1]*1.6 and px[0]>px[2]*1.6
    if red:mask=caliper_mask;selected['caliper']+=1
    elif abs(c.y)>.80:mask=rim_mask;selected['rim']+=1
    else:continue
    for j in range(1,len(points)-1):triangle(mask,[points[0],points[j],points[j+1]])
arr[caliper_mask,:3]=np.array([.085,.09,.096])
def resample_mask(m):
    yi=np.minimum((np.arange(oh)*h/oh).astype(int),h-1);xi=np.minimum((np.arange(ow)*w/ow).astype(int),w-1);return m[yi[:,None],xi[None,:]]
rm=resample_mask(rim_mask);cm=resample_mask(caliper_mask);metal=oa[:,:,2]>.5
oa[:,:,1][rm&metal]=np.clip(oa[:,:,1][rm&metal]*.45+.24,.26,.40)
oa[:,:,1][cm]=.44;oa[:,:,2][cm]=.72
def saveimage(name,a,noncolor):
    im=bpy.data.images.new(name,width=a.shape[1],height=a.shape[0],alpha=True);im.colorspace_settings.name='Non-Color'if noncolor else 'sRGB';im.pixels.foreach_set(a.ravel());im.filepath_raw=str(R/'exports'/(name+'.png'));im.file_format='PNG';im.save();return im
newd=saveimage('T_MKZ_Exterior02_BaseColor',arr,False);newo=saveimage('T_MKZ_Exterior02_ORM',oa,True)
for n in nodes:
    if n.type=='TEX_IMAGE'and n.image==albedo:n.image=newd
    elif n.type=='TEX_IMAGE'and n.image==orm:n.image=newo
extras=[];groups={}
def finish(o,name,m,group):
    o.name=name;o.data.materials.append(m)
    for p in o.data.polygons:p.use_smooth=True
    extras.append(o);groups.setdefault(group,[]).append(o);return o
def cylinder(name,loc,radius,depth,m,group,vertices=32):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=depth,location=loc,rotation=(math.pi/2,0,0));o=finish(bpy.context.object,name,m,group);be=o.modifiers.new('Machined edge','BEVEL');be.width=.0007;be.segments=3;return o
def torus(name,loc,major,minor,m,group):
    bpy.ops.mesh.primitive_torus_add(major_segments=96,minor_segments=12,major_radius=major,minor_radius=minor,location=loc,rotation=(math.pi/2,0,0));return finish(bpy.context.object,name,m,group)
for name,c in wheelheads.items():
    sign=1 if c.y>0 else -1;group='SM_MKZ_Hardware_'+name;center=ray((c.x,sign*2,c.z),(0,-sign,0));facey=(center.y if center else sign*.856)
    for j in range(5):
        a=math.radians(-90+72*j);x=c.x+.052*math.cos(a);z=c.z+.052*math.sin(a);p=ray((x,sign*2,z),(0,-sign,0));y=(p.y if p else facey)+sign*.004
        cylinder('HexLug_'+name+str(j),(x,y,z),.0083,.009,steel,group,6)
        cylinder('LugCap_'+name+str(j),(x,y+sign*.005,z),.0063,.002,steel,group)
    a=math.radians(235);x=c.x+.205*math.cos(a);z=c.z+.205*math.sin(a);p=ray((x,sign*2,z),(0,-sign,0));y=(p.y if p else facey)+sign*.008
    cylinder('ValveStem_'+name,(x,y,z),.004,.018,rubber,group)
    cylinder('ValveCap_'+name,(x,y+sign*.012,z),.0045,.007,rubber,group,16)
    # A real shallow annular brake surface, with radial machining rings. Behind the wheel spokes.
    gr='SM_MKZ_Rotor_'+name;ry=sign*.786
    for j in range(9):torus('BrakeMachining_'+name+str(j),(c.x,ry,c.z),.108+j*.0065,.00038,rotor,gr)
    # Export each rotating hardware set in wheel-local coordinates; retain assembly positions in Blender.
    for key in [group,gr]:
        bpy.ops.object.select_all(action='DESELECT')
        for o in groups[key]:o.select_set(True)
        bpy.context.view_layer.objects.active=groups[key][0];bpy.ops.object.convert(target='MESH');bpy.ops.object.join();o=bpy.context.object;o.name=key;groups[key]=[o]
        mw=o.matrix_world.copy();S.cursor.location=c;bpy.ops.object.origin_set(type='ORIGIN_CURSOR');o.location-=c
        bpy.ops.export_scene.fbx(filepath=str(R/'exports'/(key+'.fbx')),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')
        o.location+=c
# New optical elements on the lower LED arrays; placed on the existing surface.
gr='SM_MKZ_Optical_Details'
for sign in [-1,1]:
    for j in range(6):
        y=sign*(.565+j*.049);z=.434;p=ray((3,y,z),(-1,0,0))
        if p is None:continue
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24,ring_count=12,location=(p.x+.004,y,z));o=bpy.context.object;o.scale=(.004,.012,.012);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);finish(o,'OpticLens'+str(sign)+'_'+str(j),lens,gr)
bpy.ops.object.select_all(action='DESELECT')
for o in groups[gr]:o.select_set(True)
bpy.context.view_layer.objects.active=groups[gr][0];bpy.ops.object.join();o=bpy.context.object;o.name=gr;S.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');groups[gr]=[o]
bpy.ops.export_scene.fbx(filepath=str(R/'exports'/(gr+'.fbx')),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z')
report={'result':'AUTHORED_PASS_1','original_mesh_geometry_preserved':True,'new_hardware_sets':list(groups),'texture_selection':selected,'changed_caliper_pixels':int(caliper_mask.sum()),'rim_mask_pixels':int(rim_mask.sum()),'new_geometry':{k:{'vertices':len(v[0].data.vertices),'faces':len(v[0].data.polygons)}for k,v in groups.items()},'paint':{'base_color_linear':[.024,.045,.065],'roughness':.19,'clearcoat':1,'coat_roughness':.075},'scope':'Visual approximation; original tire size, silhouette, rig and physics retained.'}
(R/'authored-pass1.json').write_text(json.dumps(report,indent=2))
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Refined.blend'))
for name in ['front-three-quarter','rear-three-quarter','front-detail','wheel-detail']:
    S.camera=bpy.data.objects['Camera_'+name];S.render.filepath=str(R/'renders'/('refined-'+name+'.png'));bpy.ops.render.render(write_still=True)
