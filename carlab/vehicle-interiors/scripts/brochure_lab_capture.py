    latest={}
    (ROOT/'.brochure-lab-stop').unlink(missing_ok=True)
    print('BROCHURE LAB READY',flush=True)
    deadline=time.monotonic()+1800
    while time.monotonic()<deadline and not (ROOT/'.brochure-lab-stop').exists():
        request=ROOT/'.brochure-lab-capture.json'
        if not request.exists():
            world.tick(180);time.sleep(.1);continue
        spec=json.loads(request.read_text());request.unlink()
        name=spec['name'];loc=spec.get('location',[-.42,0,1.25]);rot=spec.get('rotation',[-30,0,0]);transform=carla.Transform(carla.Location(*loc),carla.Rotation(pitch=rot[0],yaw=rot[1],roll=rot[2]))
        if spec.get('open_door',False):vehicle.open_door(carla.VehicleDoor.FR)
        else:vehicle.close_door(carla.VehicleDoor.FR)
        if 'weather' in spec:world.set_weather(carla.WeatherParameters(**spec['weather']))
        bp.set_attribute('fov',str(spec.get('fov',90)))
        for attr,value in spec.get('attributes',{}).items():
            if not bp.has_attribute(attr):raise ValueError('Unsupported camera attribute '+attr)
            bp.set_attribute(attr,str(value))
        camera=world.spawn_actor(bp,transform,attach_to=vehicle);actors.append(camera);q=queue.Queue(maxsize=8)
        def receive(im):
            if q.full():q.get_nowait()
            q.put_nowait(im)
        camera.listen(receive)
        for _ in range(spec.get('ticks',100)):
            world.tick(180)
            try:
                im=q.get(timeout=5)
                while not q.empty():im=q.get_nowait()
                latest[name]=im
            except queue.Empty:pass
        assert name in latest
        latest[name].save_to_disk(str(ROOT/'renders'/(name+'.png')))
        camera.stop()
        for _ in range(3):world.tick(180)
        camera.destroy();actors.remove(camera)
        print('CAPTURED '+name,flush=True)
