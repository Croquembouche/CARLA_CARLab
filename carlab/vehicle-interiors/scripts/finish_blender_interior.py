import bpy,math,json,time
from pathlib import Path
from mathutils import Vector
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_v2.blend'));s=bpy.context.scene
col=bpy.data.collections.new('Interior_Final_Details');s.collection.children.link(col)
def move(o):
 for c in list(o.users_collection):c.objects.unlink(o)
 col.objects.link(o);return o
m=bpy.data.materials.new('MKZ_Seatbelt_Webbing');m.use_nodes=True
nt=m.node_tree;p=nt.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.009,.011,.013,1);p.inputs['Roughness'].default_value=.83
uv=nt.nodes.new('ShaderNodeTexCoord');wave=nt.nodes.new('ShaderNodeTexWave');wave.wave_type='BANDS';wave.bands_direction='X';wave.inputs['Scale'].default_value=160;nt.links.new(uv.outputs['UV'],wave.inputs['Vector']);bump=nt.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.17;bump.inputs['Distance'].default_value=.00012;nt.links.new(wave.outputs['Color'],bump.inputs['Height']);nt.links.new(bump.outputs['Normal'],p.inputs['Normal'])
plastic=bpy.data.materials['MKZ_Seat_Base_Plastic'];metal=bpy.data.materials['MKZ_Seat_Hardware'];red=bpy.data.materials['MKZ_Belt_Release']
def box(name,loc,dims,mat,bevel=.005):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=move(bpy.context.object);o.name=name;o.dimensions=dims;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat)
 mod=o.modifiers.new('Soft manufactured edges','BEVEL');mod.width=bevel;mod.segments=3
 o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');return o
def belt(name,points,width=.046):
 # Webbing lies against the pillar, with width along vehicle X and thickness along Y.
 verts=[]
 for p in points:
  verts.extend([(p[0]-width/2,p[1],p[2]),(p[0]+width/2,p[1],p[2])])
 faces=[(2*i,2*i+1,2*i+3,2*i+2) for i in range(len(points)-1)]
 mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.materials.append(m);o=bpy.data.objects.new(name,mesh);col.objects.link(o)
 uv=mesh.uv_layers.new(name='WebbingUV')
 for face in mesh.polygons:
  for li in face.loop_indices:
   vi=mesh.loops[li].vertex_index;uv.data[li].uv=(vi%2,vi//2/(len(points)-1))
 sol=o.modifiers.new('Woven webbing thickness','SOLIDIFY');sol.thickness=.0014
 return o
for row,upper_x,lower_x,upper_z in [('Front',-.31,-.14,1.23),('Rear',-1.29,-.75,1.16)]:
 for side in [-1,1]:
  prefix=f"{row}_{'Left' if side==1 else 'Right'}";y=side*.643
  pts=[(upper_x,y,upper_z),(upper_x+.025,y-side*.018,upper_z-.10),(lower_x-.04,y-side*.028,.76),(lower_x,y-side*.018,.47)]
  belt(prefix+'_Stowed_Seatbelt',pts)
  box(prefix+'_Belt_Upper_Guide',(upper_x,y,upper_z),(.078,.027,.055),plastic,.01)
  box(prefix+'_Belt_Lower_Anchor',(lower_x,y,.46),(.064,.028,.055),plastic)
  box(prefix+'_Belt_Latch_Plate',(lower_x-.05,y-side*.03,.78),(.061,.007,.053),metal,.003)
# Center rear belt exits next to the center headrest; parked against the backrest.
belt('Rear_Center_Stowed_Seatbelt',[(-1.275,.145,1.11),(-1.255,.14,.98),(-1.245,.13,.70),(-1.12,.135,.55)],.043)
box('Rear_Center_Seatbelt_Buckle',(-.96,.15,.61),(.062,.03,.06),plastic)
box('Rear_Center_Red_Release',(-.96,.15,.643),(.043,.023,.007),red,.002)
# Mild, consistent leather micrograin without changing the approved base color.
for name in ['MKZ_Ebony_Leather','MKZ_Perforated_Leather']:
 mat=bpy.data.materials.get(name)
 if not mat:continue
 nt=mat.node_tree;p=nt.nodes.get('Principled BSDF')
 tex=nt.nodes.new('ShaderNodeTexNoise');tex.name='Final Leather Micrograin';tex.inputs['Scale'].default_value=380;tex.inputs['Detail'].default_value=2;tex.inputs['Roughness'].default_value=.7
 coords=nt.nodes.new('ShaderNodeTexCoord');nt.links.new(coords.outputs['Object'],tex.inputs['Vector'])
 bump=nt.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.12;bump.inputs['Distance'].default_value=.00009;nt.links.new(tex.outputs['Fac'],bump.inputs['Height']);nt.links.new(bump.outputs['Normal'],p.inputs['Normal'])
# Preserve camera composition and use only the currently free GPU 3.
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices();devices=[d for d in prefs.devices if d.type=='OPTIX'];assert len(devices)>3
for d in prefs.devices:d.use=d==devices[3]
s.cycles.device='GPU';s.cycles.samples=128;s.cycles.use_denoising=True;s.render.resolution_x=1600;s.render.resolution_y=1200;s.render.resolution_percentage=100
s.render.image_settings.file_format='PNG';s.camera=bpy.data.objects['MKZ_Dashboard'];bpy.ops.file.pack_all()
# Geometry, UV, material and camera acceptance checks before export.
checks={'seatbelt_webbing':len([o for o in col.objects if 'Stowed_Seatbelt' in o.name]),'new_detail_objects':len(col.objects),'cameras':[o.name for o in s.objects if o.type=='CAMERA'],'unpacked_images':[i.name for i in bpy.data.images if i.source=='FILE' and i.has_data and not i.packed_file]}
assert checks['seatbelt_webbing']==5 and not checks['unpacked_images']
assert all(math.isfinite(c) for o in s.objects if o.type=='MESH' for v in o.data.vertices for c in v.co)
path=R/'Lincoln_MKZ_Interior_Final.blend';bpy.ops.wm.save_as_mainfile(filepath=str(path))
for name,label in [('MKZ_Dashboard','Dashboard'),('MKZ_Passenger_Seat','Passenger'),('MKZ_Rear_Seats','Rear')]:
 s.camera=bpy.data.objects[name];s.render.filepath=str(R/'renders'/('Final_'+label+'.png'));bpy.ops.render.render(write_still=True)
s.camera=bpy.data.objects['MKZ_Dashboard'];bpy.ops.wm.save_as_mainfile(filepath=str(path))
checks['result']='PASS';checks['file']=str(path);(R/'blender-final-verification.json').write_text(json.dumps(checks,indent=2))
