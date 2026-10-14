# Lincoln MKZ moonroof correction

The installed `vehicle.lincoln.mkz_interior` Blueprint had tinted moonroof glass, but the removed stock roof-glass region was not bounded by an opaque roof and headliner surround. From the cabin this made the vehicle look as though the roof was missing.

The corrected Blueprint now includes `MoonroofFrame`, a single watertight static mesh around the existing bounded glass panel. Its exterior faces use the stock Lincoln MKZ bodywork material and its cabin-facing faces use the existing headliner material. The tinted `MoonroofGlass` component remains enabled.

The glass is also a closed collision surface. `MoonroofGlass` uses `QueryAndPhysics`, the `BlockAll` collision profile, and complex-as-simple collision so traces and rain collision follow the curved glass panel instead of treating the moonroof opening as empty space.

## Verification

- Blueprint: `/Game/VehicleInteriors/Lincoln/BP_LincolnMKZ_Interior`
- CARLA ID: `vehicle.lincoln.mkz_interior`
- Imported mesh: `/Game/VehicleInteriors/Lincoln/SM_MKZ_Moonroof_Frame`
- Mesh: 320 vertices, 320 polygons
- Opening bounds: x `[-0.88, 0.30]` m, y `[-0.485, 0.485]` m
- Clean-server spawn: passed on Town10HD_Opt
- Physics: four wheels and expected vehicle bounds passed
- Camera validation: 65 warm-up ticks per view at Epic scalability
- Visual review: cabin roof/headliner surround present; tinted center glass present; painted exterior roof surround present
- Engine shutdown after isolated validation: clean, return code 0
- Moonroof glass collision: `QueryAndPhysics`, `BlockAll`, `ComplexAsSimple`

The production control-center scene was restored from its pre-change checkpoint with 33 managed actors and six sensors. Four GPU workers initialized and verified sensor frames. The simulation was resumed and advanced from frame 353 to frame 363 without a controller error.

## Evidence

- `renders/carla_moonroof.png`
- `renders/carla_roof_exterior.png`
- `renders/live_cabin_overview_after_moonroof_fix.jpg`
- `moonroof-frame-build.json`
- `moonroof-frame-install.json`
- `moonroof-collision-install.json`
- `renders/live_moonroof_collision_rain_check.jpg`
- `runtime-report.json`
