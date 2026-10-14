import bpy,numpy as np,json,shutil
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');I=R/'iterations/pass2';I.mkdir(parents=True,exist_ok=True)
for f in (R/'renders').glob('refined-*.png'):shutil.copy2(f,I/f.name)
bpy.ops.wm.open_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Refined.blend'));S=bpy.context.scene
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
for d in prefs.devices:d.use=d.type=='OPTIX'
S.cycles.device='GPU'
o=bpy.data.objects.get('SM_MKZ_Optical_Details')
if o:bpy.data.objects.remove(o,do_unlink=True)
f=R/'exports/SM_MKZ_Optical_Details.fbx'
if f.exists():shutil.move(str(f),str(I/f.name))
im=next(i for i in bpy.data.images if i.name.startswith('T_MKZ_Exterior02_BaseColor'));w,h=im.size;a=np.empty(w*h*4,np.float32);im.pixels.foreach_get(a);a=a.reshape(h,w,4)
m=(a[:,:,0]>a[:,:,1]*1.4)&(a[:,:,0]>a[:,:,2]*1.4)&(a[:,:,0]>.015);yy,xx=np.mgrid[:h,:w];m&=(xx<w*.19)&(yy>h*.60)&(yy<h*.77);a[m,:3]=[.085,.09,.096];im.pixels.foreach_set(a.ravel());im.filepath_raw=str(R/'exports/T_MKZ_Exterior02_BaseColor.png');im.file_format='PNG';im.save()
(R/'authored-final.json').write_text(json.dumps({'result':'FINAL_SOURCE','rejected_optical_additions':'Measured additions still overlapped the existing caps after refraction. Removed; original authored optics are retained.','caliper_seam_pixels_repaired':int(m.sum()),'new_geometry':'Five hex fasteners and a valve for each wheel; concentric brake machining rings.','paint_color_preserved':True,'original_body_rig_physics_preserved':True},indent=2))
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(R/'Lincoln_MKZ_Exterior_Refined.blend'))
for name in ['front-three-quarter','rear-three-quarter','front-detail','wheel-detail']:
    S.camera=bpy.data.objects['Camera_'+name];S.render.filepath=str(R/'renders'/('refined-'+name+'.png'));bpy.ops.render.render(write_still=True)
