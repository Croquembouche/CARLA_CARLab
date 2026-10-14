import unreal as u,json
from pathlib import Path
R=Path('/mnt/simulations/vehicle-exteriors/lincoln-mkz');stop=R/'.stop-2250'
(R/'observer-started.txt').write_text('Python observer initialized')
colored=set()
def tick(delta):
    world=u.find_object(None,'/Game/VehicleExteriors/LincolnMKZ/L_ExteriorStudio.L_ExteriorStudio')
    (R/'observer-tick.json').write_text(json.dumps({'found_world':bool(world)}))
    if not world:return
    if stop.exists():stop.unlink();u.SystemLibrary.execute_console_command(world,'quit');return
    report=[]
    for a in u.GameplayStatics.get_all_actors_of_class(world,u.CarlaWheeledVehicle):
        if 'LincolnMKZ'not in a.get_class().get_name():continue
        if a.get_name()not in colored:
            for c in a.get_components_by_class(u.MeshComponent):
                for i,m in enumerate(c.get_materials()):
                    if m and ('Bodywork'in m.get_name()or 'ClearcoatPaint'in m.get_name()):
                        mid=c.create_dynamic_material_instance(i,m);mid.set_vector_parameter_value(u.Name('Base Color'),u.LinearColor(.024,.045,.065,1));c.set_material(i,mid)
            colored.add(a.get_name())
        if 'Refined'not in a.get_class().get_name():continue
        entry={'actor':a.get_name(),'components':[]}
        for c in a.get_components_by_class(u.MeshComponent):
            if c.get_name().startswith('SM_MKZ_')or c.get_name()=='VehicleMesh':
                v=c.get_world_location();rot=c.get_world_rotation();parent=c.get_attach_parent();socket=c.get_attach_socket_name();expected=parent.get_socket_location(socket)if parent and str(socket)!='None'else v;entry['components'].append({'name':c.get_name(),'socket':str(socket),'location_cm':[v.x,v.y,v.z],'rotation_deg':[rot.pitch,rot.yaw,rot.roll],'parent_socket_location_cm':[expected.x,expected.y,expected.z],'materials':[m.get_path_name()if m else None for m in c.get_materials()]})
        report.append(entry)
    if report:(R/'runtime-components.json').write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
