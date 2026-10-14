import bpy,bmesh,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-interiors');bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));s=bpy.context.scene
stock=next(o for o in s.objects if o.name.startswith('ReferenceGlazing_SM_LincolnMKZ_Glass_Ext1') and len(o.data.vertices)>100)
bm=bmesh.new();bm.from_mesh(stock.data);removed=[]
for f in bm.faces:
 p=stock.matrix_world@f.calc_center_median()
 if -.98<p.x<.4 and abs(p.y)<.6 and p.z>1.42:removed.append(f)
assert len(removed)>30,len(removed)
bmesh.ops.delete(bm,geom=removed,context='FACES');bm.to_mesh(stock.data);bm.free()
# Maintain the editable visible stock windows in Blender, with the new roof pane separate.
bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Completed.blend'));bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Interior_Final.blend'))
# Reimport original source for preserved two-slot glass/emissive layout in the UE export.
bpy.ops.wm.read_factory_settings(use_empty=True);s=bpy.context.scene;bpy.ops.import_scene.fbx(filepath=str(R/'reference/glazing/SM_LincolnMKZ_Glass_Ext1.fbx'))
o=next(o for o in s.objects if o.type=='MESH' and len(o.data.vertices)>100);bm=bmesh.new();bm.from_mesh(o.data);cut=[]
for f in bm.faces:
 p=o.matrix_world@f.calc_center_median()
 if -.98<p.x<.4 and abs(p.y)<.6 and p.z>1.42:cut.append(f)
assert len(cut)==len(removed),(len(cut),len(removed));bmesh.ops.delete(bm,geom=cut,context='FACES');bm.to_mesh(o.data);bm.free()
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o;o.name='SM_MKZ_Glass_Without_Moonroof';bpy.ops.export_scene.fbx(filepath=str(R/'exports/SM_MKZ_Glass_Without_Moonroof.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,axis_forward='-Y',axis_up='Z');bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Glazing_Export.blend'))
(R/'roof-overlap-removal.json').write_text(json.dumps({'removed_roof_faces':len(cut),'remaining_faces':len(o.data.polygons),'materials':[m.name for m in o.data.materials]},indent=2))
