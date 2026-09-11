import unreal as u,json
D='/Game/Carla/Static/Car/4Wheeled/LincolnMKZ/SK_LincolnMKZ'
s=u.load_asset(D);c=u.SkeletalMeshComponent();c.set_skeletal_mesh_asset(s)
r={'skeleton':str(s.skeleton.get_path_name()),'physics_asset':str(s.physics_asset.get_path_name()),'bones':[str(c.get_bone_name(i)) for i in range(c.get_num_bones())],'materials':[str(m.material_interface.get_path_name()) for m in s.materials]}
open('/mnt/simulations/vehicle-interiors/vehicle-rig-original.json','w').write(json.dumps(r,indent=2))
