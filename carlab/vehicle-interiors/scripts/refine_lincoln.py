"""Reference-led MKZ material and close-camera revision. Preserves v1 workspace."""
import bpy, math, json, sys
from pathlib import Path
from mathutils import Vector
ROOT=Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Lincoln_Interior_Mannequin.blend'))
scene=bpy.context.scene

def mat(name,color,rough=.4,metal=0,grain=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 nt=m.node_tree;p=nt.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
 if grain:
  coord=nt.nodes.new('ShaderNodeTexCoord');noise=nt.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=1400;noise.inputs['Detail'].default_value=2
  nt.links.new(coord.outputs['Object'],noise.inputs['Vector']);bump=nt.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.16;bump.inputs['Distance'].default_value=grain
  nt.links.new(noise.outputs['Fac'],bump.inputs['Height']);nt.links.new(bump.outputs['Normal'],p.inputs['Normal'])
 return m
leather=mat('MKZ_Ebony_Leather',(.018,.021,.024),.4,grain=.00012)
headliner=mat('MKZ_Charcoal_Headliner',(.033,.035,.039),.88,grain=.00009)
dash=mat('MKZ_SoftTouch_Dashboard',(.022,.025,.029),.57,grain=.00009)
thread=mat('MKZ_Stitch_Charcoal',(.09,.092,.094),.76)
wood=mat('MKZ_Walnut_Veneer',(.12,.047,.016),.28)
nt=wood.node_tree;p=nt.nodes.get('Principled BSDF');p.inputs['Coat Weight'].default_value=.4;p.inputs['Coat Roughness'].default_value=.23
co=nt.nodes.new('ShaderNodeTexCoord');mapping=nt.nodes.new('ShaderNodeVectorMath');mapping.operation='MULTIPLY';mapping.inputs[1].default_value=(2,50,220);nt.links.new(co.outputs['Generated'],mapping.inputs[0]);n=nt.nodes.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=3;n.inputs['Detail'].default_value=3;n.inputs['Roughness'].default_value=.65;nt.links.new(mapping.outputs[0],n.inputs['Vector']);r=nt.nodes.new('ShaderNodeValToRGB');r.color_ramp.elements[0].position=.2;r.color_ramp.elements[0].color=(.035,.012,.004,1);r.color_ramp.elements[1].position=.8;r.color_ramp.elements[1].color=(.19,.073,.025,1);nt.links.new(n.outputs['Fac'],r.inputs[0]);nt.links.new(r.outputs[0],p.inputs['Base Color'])
# Preserve screen/labels/metal from the original atlas; lower excessive normal amplitude.
for m in bpy.data.materials:
 if not m.use_nodes or not m.name.startswith('MI_LincolnMKZ_Interior'):continue
 for n in m.node_tree.nodes:
  if n.type=='NORMAL_MAP':n.inputs['Strength'].default_value=.09
# Pixel masks retain atlas borders, including controls whose triangles span multiple regions.
m=bpy.data.materials['MI_LincolnMKZ_Interior01'];nt=m.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
d=next(n for n in nt.nodes if n.type=='TEX_IMAGE' and n.image.name.endswith('_d.tga'))
orm=next(n for n in nt.nodes if n.type=='SEPARATE_COLOR')
split=nt.nodes.new('ShaderNodeSeparateColor');nt.links.new(d.outputs['Color'],split.inputs[0])
def mathnode(op,a,b):
 n=nt.nodes.new('ShaderNodeMath');n.operation=op
 for i,x in enumerate([a,b]):
  if isinstance(x,(float,int)):n.inputs[i].default_value=x
  else:nt.links.new(x,n.inputs[i])
 return n.outputs[0]
def mix(f,a,b):
 n=nt.nodes.new('ShaderNodeMixRGB');n.blend_type='MIX'
 nt.links.new(f,n.inputs[0])
 for i,x in enumerate([a,b],1):
  if isinstance(x,(tuple,list)):n.inputs[i].default_value=(*x,1)
  else:nt.links.new(x,n.inputs[i])
 return n.outputs[0]
red,green,blue=[split.outputs[i] for i in range(3)]
beige=mathnode('MULTIPLY',mathnode('GREATER_THAN',red,.065),mathnode('MULTIPLY',mathnode('GREATER_THAN',red,mathnode('MULTIPLY',green,1.07)),mathnode('GREATER_THAN',green,mathnode('MULTIPLY',blue,1.07))))
brown=mathnode('MULTIPLY',mathnode('LESS_THAN',red,.065),mathnode('MULTIPLY',mathnode('GREATER_THAN',red,.006),mathnode('GREATER_THAN',red,mathnode('MULTIPLY',green,1.18))))
veneer=mathnode('MULTIPLY',brown,mathnode('LESS_THAN',orm.outputs['Green'],.17))
wood_uv=nt.nodes.new('ShaderNodeTexCoord');wood_scale=nt.nodes.new('ShaderNodeVectorMath');wood_scale.operation='MULTIPLY';wood_scale.inputs[1].default_value=(7,280,1);nt.links.new(wood_uv.outputs['UV'],wood_scale.inputs[0])
wood_noise=nt.nodes.new('ShaderNodeTexNoise');wood_noise.inputs['Scale'].default_value=4;wood_noise.inputs['Detail'].default_value=3;nt.links.new(wood_scale.outputs[0],wood_noise.inputs['Vector'])
wood_ramp=nt.nodes.new('ShaderNodeValToRGB');wood_ramp.color_ramp.elements[0].color=(.04,.013,.004,1);wood_ramp.color_ramp.elements[1].color=(.18,.075,.027,1);nt.links.new(wood_noise.outputs['Fac'],wood_ramp.inputs[0])
color=mix(brown,d.outputs['Color'],(.022,.025,.029));color=mix(veneer,color,wood_ramp.outputs[0]);color=mix(beige,color,(.018,.021,.024));nt.links.new(color,p.inputs['Base Color'])
rough=mix(brown,orm.outputs['Green'],(.57,.57,.57));rough=mix(veneer,rough,(.28,.28,.28));rough=mix(beige,rough,(.44,.44,.44));nt.links.new(rough,p.inputs['Roughness'])
# Keep authored seam/crease normals on upholstery, but remove the exaggerated pebble texture.
# New geometry for double stitching follows the original seat surfaces by ray casting.
car=bpy.data.objects['SK_LincolnMKZ.001'];inv=car.matrix_world.inverted()
def line(name,points,radius=.0004):
 if len(points)<2:return
 curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.resolution_u=2;curve.bevel_depth=radius;curve.bevel_resolution=2
 sp=curve.splines.new('POLY');sp.points.add(len(points)-1)
 for pnt,co in zip(sp.points,points):pnt.co=(*co,1)
 ob=bpy.data.objects.new(name,curve);scene.collection.objects.link(ob);curve.materials.append(thread)
for row,origin,zlow,zhigh in [('Front',.30,.71,1.10),('Rear',-.90,.69,1.025)]:
 for side in [-1,1]:
  for edge in [-1,1]:
   for parallel in [0,.004]:
    y=side*.373+edge*(.13+parallel);points=[]
    for step in range(81):
     z=zlow+(zhigh-zlow)*step/80
     hit,co,no,idx=car.ray_cast(inv@Vector((origin,y,z)),inv.to_3x3()@Vector((-1,0,0)))
     if hit:
      world=car.matrix_world@co
      if origin-.55<world.x<origin:points.append((world.x+.002,y,z))
    # Short segments read as actual thread instead of a continuous plastic stripe.
    for j in range(0,len(points)-1,2):line(row+'_Backrest_Stitch',points[j:j+2])
  for edge in [-1,1]:
   for parallel in [0,.004]:
    y=side*.373+edge*(.13+parallel);points=[]
    for step in range(65):
     x=(.08 if row=='Front' else -.98)+.35*step/64
     hit,co,no,idx=car.ray_cast(inv@Vector((x,y,.68)),inv.to_3x3()@Vector((0,0,-1)))
     if hit:
      world=car.matrix_world@co
      if .52<world.z<.67:points.append((x,y,world.z+.002))
    for j in range(0,len(points)-1,2):line(row+'_Cushion_Stitch',points[j:j+2])
report={'finish':'Ebony and walnut reference study','stitch_geometry':len([o for o in scene.objects if 'Stitch' in o.name]),'source':'2020 Lincoln MKZ brochure pages 3 and 4'}
exec(compile((ROOT/'scripts/seat_upgrade.py').read_text(),str(ROOT/'scripts/seat_upgrade.py'),'exec'))
# Present the cabin without the mannequin obscuring the dashboard.
bpy.data.objects['SK_SeatedMannequin'].hide_render=True
# New camera avoids blocking the center stack with the front seatbacks.
def camera(name,loc,target,lens):
 bpy.ops.object.camera_add(location=loc);o=bpy.context.object;o.name=name;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();o.data.lens=lens;o.data.clip_start=.012;return o
cams=[camera('MKZ_Dashboard',(-.20,0,1.25),(1.1,0,.86),19),camera('MKZ_Driver_View',(.23,.40,1.28),(1.20,.15,.90),22),camera('MKZ_Rear_Seats',(-.43,-.05,1.22),(-1.20,0,.92),15),camera('MKZ_Front_Seats',(.60,-.60,1.25),(-.15,.04,.92),19)]
# Light entering the actual apertures gives the trim and seats useful highlights.
for o in list(scene.objects):
 if o.type=='LIGHT':bpy.data.objects.remove(o,do_unlink=True)
for name,loc,target,energy,size in [('Window_Key',(.5,-1.8,1.85),(-.1,.2,.75),110,2.2),('Windshield_Daylight',(1.8,.3,2.1),(0,0,.7),180,2.4),('Rear_Window',(-2,0,1.8),(-.8,0,.8),80,1.8)]:
 bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.name=name;o.data.energy=energy;o.data.size=size;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.72,.85,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.3
scene.render.engine='CYCLES';scene.cycles.samples=96;scene.cycles.use_denoising=True
prefs=bpy.context.preferences.addons['cycles'].preferences
try:
 prefs.compute_device_type='OPTIX';prefs.get_devices()
 optix=[d for d in prefs.devices if d.type=='OPTIX']
 for d in prefs.devices:d.use=bool(optix) and d==optix[min(1,len(optix)-1)]
 scene.cycles.device='GPU'
except Exception:scene.cycles.device='CPU'
scene.render.resolution_x=1440;scene.render.resolution_y=900;scene.render.resolution_percentage=100
scene.camera=cams[0]
for area in bpy.context.screen.areas:
 if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.shading.type='MATERIAL'
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Lincoln_MKZ_Interior_v2.blend'))
(ROOT/'refinement-report.json').write_text(json.dumps(report,indent=2))
for cam in (cams[:1] if '--preview' in sys.argv else cams):
 scene.camera=cam;scene.render.filepath=str(ROOT/'renders'/(cam.name+'.png'));bpy.ops.render.render(write_still=True)
print('MKZ_REFINEMENT_COMPLETE')
