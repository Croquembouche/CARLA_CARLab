"""Matching empty-cabin reference views using the real CARLA sensor renderer."""
import os
from pathlib import Path
os.environ['LINCOLN_BROCHURE_CAPTURE']='1'
R=Path('/mnt/simulations/vehicle-interiors');s=(R/'scripts/verify_carla.py').read_text()
s=s.replace('runtime-report.json','brochure-runtime-report.json').replace("'runtime.log'", "'brochure-runtime.log'")
s=s.replace('py /mnt/simulations/vehicle-interiors/scripts/test_session_shutdown.py', 'py /mnt/simulations/vehicle-interiors/scripts/test_session_shutdown.py,py /mnt/simulations/vehicle-interiors/scripts/brochure_presentation_runtime.py')
s=s.replace('cloudiness=20,sun_altitude_angle=55,sun_azimuth_angle=170','cloudiness=100,sun_altitude_angle=38,sun_azimuth_angle=250')
s=s.replace('get_spawn_points()[0]','get_spawn_points()[8]')
s=s.replace("bp.set_attribute('fov','90')","bp.set_attribute('fov','100')").replace("'LincolnInterior'","'LincolnBrochure'")
s=s.replace("'1280'","'1600'").replace('1280*800*4','1600*800*4')
a=s.index('    views={');b=s.index('    latest={}',a)
s=s[:a]+'''    views={
        'brochure_dashboard':carla.Transform(carla.Location(x=-.12,y=0,z=1.25),carla.Rotation(pitch=-27,yaw=0)),
        'brochure_cabin':carla.Transform(carla.Location(x=.4,y=.80,z=1.16),carla.Rotation(pitch=-7,yaw=-116)),
    }
'''+s[b:]
s=s.replace('range(65)','range(150)').replace('65 warm-up ticks','150 warm-up ticks')
s=s.replace('        camera=world.spawn_actor', "        if name=='brochure_cabin':vehicle.open_door(carla.VehicleDoor.FR);bp.set_attribute('fov','120')\n        camera=world.spawn_actor")
if os.environ.get('LINCOLN_BROCHURE_LAB')=='1':
 a=s.index('    views={');b=s.index('    physics=vehicle.get_physics_control()',a)
 s=s[:a]+(R/'scripts/brochure_lab_capture.py').read_text()+s[b:]
exec(compile(s,'verify_carla.py','exec'))

if os.environ.get('LINCOLN_BROCHURE_LAB')!='1':
 import json
 path=R/'brochure-runtime-report.json';report=json.loads(path.read_text());report['presentation']={'post_process_profile':'LincolnBrochure','temporary_lighting_level':'/Game/VehicleInteriors/Lincoln/L_MKZ_Photography','empty_cabin':True,'passenger_door_open_for_side_view':True,'spawn_index':8,'resolution':[1600,800],'fov_degrees':{'dashboard':100,'cabin':120}};path.write_text(json.dumps(report,indent=2))
