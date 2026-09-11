# Lincoln MKZ interior — final Blender model

The current editable model is `Lincoln_MKZ_Interior_Final.blend`. Textures are packed.
The finishing pass adds five stowed seatbelts, guides, anchors, buckle hardware,
and subtle leather surface detail while retaining the reviewed seat and trim design.
The mannequin and seat markers remain available; Quest work is deferred.

## Deliverables

- `Lincoln_MKZ_Interior_Final.blend`: final editable cabin and mannequin scene.
- `renders/Final_Dashboard.png`, `Final_Passenger.png`, `Final_Rear.png`: reviewed Blender renders.
- `exports/SM_MKZ_Cabin_Final.fbx`: dashboard/cabin section, replacement seats, trim and belts.
- `exports/SK_MKZ_ExteriorOnly.fbx`: exterior and wheel rig with the original cabin section removed.
- `Lincoln_MKZ_Exterior_Rig.blend`: saved centimeter-scale exterior export rig.
- `final-rig-compatibility.json`: all nine reference bone positions, rotations and scales compared with stock.
- `exports/T_MKZ_Ebony_BaseColor.png`, `T_MKZ_Ebony_ORM.png`: 4096px material atlases.
- `blender-final-verification.json`: packed-image, geometry and seatbelt checks.
- `final-unreal-import.json`: saved Unreal assets, bone compatibility and constructed Blueprint checks.
- `runtime-report.json`: runtime capture status; acceptance requires manual image review.

## CARLA integration

The custom Blueprint is `/Game/VehicleInteriors/Lincoln/BP_LincolnMKZ_Interior`.
Its CARLA identifier is `vehicle.lincoln.mkz_interior`.
It now uses `SK_MKZ_Exterior_Final` and `SM_MKZ_Cabin_Final`, sharing the stock
Lincoln skeleton and physics asset. The exterior preserves the stock three material
slots, but physically omits the old cabin faces to prevent overlapping interiors.
Original stock vehicle assets are preserved. The redundant editor mannequin
preview is removed from the Blueprint to prevent CARLA from treating it as a
motorcycle rider when calculating vehicle bounds. The runtime poseable mannequin
is preserved. The earlier material-mask approach
is superseded. The separate door components retain their meshes and hinges.

**Runtime camera and physics checks pass.** The custom vehicle spawns with four
wheels and stock-sized bounds. Fresh `renders/carla_*.png` show the dashboard,
passenger seat, rear seats and test mannequin. The baked textures use
`MI_MKZ_Final_Interior`, a material instance of the stock Lincoln master; the
earlier standalone PBR shader is superseded. The final isolated test exited normally with return code 0.
`runtime-report.json` records the successful spawn, four-wheel physics, bounds,
manual review of all three RGB captures, and clean shutdown.

With the updated project running in CARLA, spawn the variant using:

```bash
/mnt/simulations/venvs/carla/bin/python \
  /mnt/simulations/vehicle-interiors/scripts/spawn_lincoln.py --port 2000
```

Use `--spawn-index 1` (or another free map spawn) if occupied. The script leaves
its parked car in the session and does not change world settings. A synchronous
session needs its existing tick owner to keep running. A server already holding
older assets must load the updated project assets in a fresh session.

## Reproducing this revision

Run Blender scripts sequentially, with no other Blender instance open:
`finish_blender_interior.py`, then `export_final_interior.py`. The first saves
and renders the final `.blend`; the second produces the cabin and exterior FBXs
and calls `fix_exterior_export.py` for the centimeter-scale physics-compatible rig.
`scripts/import_final_interior.py` imports and assigns the final Unreal assets;
set `LINCOLN_REIMPORT=1` when intentionally replacing already imported FBX assets.
`scripts/use_stock_cabin_master.py` creates and assigns the final material instance.
Use that shader path for the baked atlas; the older `fix_cabin_pbr.py` is diagnostic
and should not be used to overwrite the final material assignments.
Runtime checks use port 2230 and the `LincolnInteriorDDC` cache graph, in-process
shader compilation and global Nanite enabled. Only one RGB camera is active at a
time to fit the test GPU. Do not disable global Nanite: the local CARLA
segmentation renderer has crashed under that diagnostic configuration.

The seats and trim are modeled approximations fitted to the CARLA vehicle,
not manufacturer CAD or scans. Close-up dashboard texture detail remains limited
by the source asset. Blender procedural leather detail is richer than the Unreal
material translation; actual CARLA RGB captures determine runtime acceptance.

Visual references:
- [Lincoln 2020 MKZ brochure, pages 3–4](https://www.auto-brochures.com/makes/Lincoln/MKZ/Lincoln_US%20MKZ_2020.pdf)
- [Edmunds interior gallery](https://www.edmunds.com/lincoln/mkz/2020/pictures/interior/)

---

# Original mannequin implementation (reference)

This example uses the installed CARLA Lincoln MKZ cabin as the geometric basis,
exports it into Blender with its interior textures, and adds a separate articulated
test mannequin and cabin detail mesh. It is not a claim of a newly reconstructed,
manufacturer-accurate Lincoln CAD model. The supplied cabin already has seats,
dashboard, instrument controls, door panels, pillars, headliner, and steering wheel.

## Files

- `Lincoln_Interior_Mannequin.blend`: editable cabin, packed interior textures,
  rigged mannequin, five seat markers, and three interior camera views.
- `exports/SK_SeatedMannequin.fbx`: rigid segmented mannequin, with explicit
  head, eyes, eyelids, upper arms, forearms, hands, pelvis, and legs.
- `exports/SM_LincolnCabinDetails.fbx`: separate floor mat detail mesh.
- `reference/`: stock mesh and textures exported from the local CARLA assets;
  `audit.json` and `blender-audit.json` preserve the inspected component/geometry data.
- `seat-layout.json`: named seat locations in Blender meters and Unreal centimeters.
- `renders/Final_*.png`: Blender renders; `renders/carla_*.png`: actual CARLA RGB sensor captures.
- `import-report.json`: created after successful UE import and factory registration.
- `backups/VehicleParameters.json`: pre-registration catalogue backup.

Unreal assets are installed under `/Game/VehicleInteriors/Lincoln`.
The example vehicle blueprint is `BP_LincolnMKZ_Interior`, a copy of the installed
Lincoln blueprint. Its new CARLA recipe is `vehicle.lincoln.mkz_interior`.
The stock Lincoln blueprint and stock vehicle mesh remain the originals.

## Editing in Blender

Open the blend file using `blender-carla`. The `Armature` object has `blink`
(0–1), `look_yaw`, and `look_pitch` custom properties. These drive eyelid closure
and head orientation in Blender. The arm bones can also be posed in Pose Mode.
Seat markers are `Seat_driver`, `Seat_front_passenger`, `Seat_rear_left`,
`Seat_rear_center`, and `Seat_rear_right`. Move the armature origin to the desired
marker to change seats; the source mesh remains local to the armature.

Blender coordinates are +X forward, +Y left, +Z up, meters. Unreal coordinates
are +X forward, +Y right, +Z up, centimeters. Do not apply an additional 100x
scale after FBX import. Use the calibrated layout rather than bounding-box centers.

## Runtime controls

`InteriorOccupantComponent` exposes these Blueprint-callable functions:

- `SetSeat(Index)`: driver, front passenger, rear left, rear center, rear right.
- `SetLook(Yaw, Pitch)`: synthetic head orientation, limited to useful seated ranges.
- `Blink()`: a short blink. `BlinkWeight` can hold eyes closed; `bAutoBlink` adds
  periodic blinks. These signals are synthetic, not physiological measurements.
- `SetHandTarget(bLeft, Position, Rotation)`: seat-local target in UE centimeters;
  a two-segment arm solver clamps unreachable positions.
- `CalibrateXR()`: align the current tracked head position/yaw to the selected seat.
- `ActivateVR()`: activate the attached HMD camera if a headset runtime is connected.

For a desktop example, enable `Enable Desktop Controls` on the occupant component
of just the vehicle being tested. F6 blinks, F7 cycles seats, F8 recenters XR, F9
activates VR, and I/K/J/L turn the head up/down/left/right. These avoid the stock
CARLA B/C/R driving, weather, and restart shortcuts.

With XR enabled, the head follows the HMD and the arms follow the left/right
motion controller grip poses. Right A triggers a blink, left X cycles seats,
left Y recenters, and the right thumbstick supplies synthetic eye pitch/yaw.
Controller binding and tracking validity must be checked on the actual headset.
This first mannequin has rigid fingers; it does not yet implement finger curling,
steering-wheel grasp constraints, or full-body motion capture.

## Quest 2 connection and limits

Quest 2 does not have eye-tracking hardware. Head direction is tracked; actual
eye direction and actual blinks are not. The explicit animation controls keep
these separate. Legs and torso use a seated reference pose; controllers do not
measure their motion. Replacement humans will need bone/face retargeting and
seat/hand calibration, rather than simply changing the mesh asset blindly.

The local Unreal project enables OpenXR and XRBase. Native immersive display
also needs a working OpenXR runtime and a connected headset. No runtime/device
was detected in the initial local check, so headset stereo, controller bindings,
latency, and comfort are not validated by the desktop asset tests.

A Linux wireless route can use ALVR and SteamVR; it requires setup on the PC
and headset. A Windows workstation can use a compatible Quest PC runtime.
Do not launch a packaged Android version of the full CARLA environment on the
Quest expecting workstation rendering performance. The intended first route
renders on the PC and streams the VR view to the headset.

VR should initially be checked with a stationary car. The existing multi-GPU
CARLA sensor setup is not automatically a low-latency stereo HMD renderer.
Camera near clipping, head visibility, head movement against cabin surfaces,
and frame timing require on-device validation before moving-car experiments.

## Original prototype build steps

These are historical setup steps. Use the final revision workflow above to
re-export or update the current cabin.

Use the installed mount alias so Unreal shares the existing build/cache paths:

```bash
/mnt/simulations/blender-5.2.1-linux-x64/blender -b -t 8 \
  --python /mnt/simulations/vehicle-interiors/scripts/build_lincoln.py

cd /mnt/simulations/UnrealEngine5_carla
Engine/Build/BatchFiles/Linux/Build.sh CarlaUnrealEditor Linux Development \
  -project=/mnt/simulations/carla/Unreal/CarlaUnreal/CarlaUnreal.uproject \
  -Module=CarlaUnreal -WaitMutex -MaxParallelActions=12

/mnt/simulations/bin/carla-editor -run=pythonscript \
  -script=/mnt/simulations/vehicle-interiors/scripts/import_unreal.py \
  -AllowCommandletRendering -RenderOffScreen -graphicsadapter=2 -unattended -nosound
```

For the first stock export run `scripts/audit_lincoln.py` in the same rendering
commandlet mode, then `scripts/inspect_blender.py` with Blender. Skeletal FBX
export in this UE build requires `-AllowCommandletRendering`; `-nullrhi` alone
hits a stock exporter assertion.

The native automation test is `Interior.Lincoln.SeatsPoseBlinkAndReach`; run it
after import. It checks actual imported bone units/handedness, all seat indices,
blink closure/reopening, finite head poses, and clamping unreachable hand targets.

## Provenance and references

The stock Lincoln mesh and textures come from the local CARLA content repository,
licensed there under Creative Commons Attribution 4.0. Credit CARLA Simulator /
Computer Vision Center for those assets. See the original content `LICENSE`.
The mannequin and mat geometry are procedurally authored by the scripts here.
Online images were used for visual reference discovery, not copied as textures.

- [CARLA UE5 vehicle authoring](https://carla-ue5.readthedocs.io/en/latest/tuto_content_authoring_vehicles/)
- [Lincoln MKZ interior reference gallery](https://www.edmunds.com/lincoln/mkz/2017/pictures/interior/)
- [Meta eye tracking device limitations](https://developers.meta.com/horizon/documentation/native/android/move-eye-tracking/)
- [Unreal OpenXR prerequisites](https://dev.epicgames.com/documentation/en-us/unreal-engine/openxr-prerequisites-in-unreal-engine)
- [ALVR Linux setup caveats](https://github.com/alvr-org/ALVR/wiki/Linux-Troubleshooting)
