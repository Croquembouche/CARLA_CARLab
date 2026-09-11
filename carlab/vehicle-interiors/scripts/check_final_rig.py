import unreal as u,json
r={}
for name,path in [('stock','/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/SK_LincolnMKZ'),('final','/Game/VehicleInteriors/Lincoln/SK_MKZ_Exterior_Final')]:
 mesh=u.load_asset(path);comp=u.SkeletalMeshComponent();comp.set_skeletal_mesh_asset(mesh)
 r[name]={'bounds':str(mesh.get_bounds()),'bones':{str(comp.get_bone_name(i)):str(comp.get_ref_pose_transform(i)) for i in range(comp.get_num_bones())}}
open('/mnt/simulations/vehicle-interiors/final-rig-poses.json','w').write(json.dumps(r,indent=2))
