import bpy
bpy.ops.wm.open_mainfile(filepath='/mnt/simulations/vehicle-interiors/Lincoln_Interior_Mannequin.blend')
for name in ['T_LincolnMKZ_Interior01_d.tga','T_LincolnMKZ_Interior01_ormh.tga']:
 im=bpy.data.images.get(name);pix=list(im.pixels);w,h=im.size
 print(name,im.colorspace_settings.name)
 for x,y in [(.277,.849),(.72,.839),(.85,.294),(.53,.95),(.765,.827),(.65,.72)]:
  i=4*(int(y*h)*w+int(x*w));print(x,y,pix[i:i+4])
