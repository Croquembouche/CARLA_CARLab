# Lincoln MKZ interior — accepted CARLA build

The user accepted the brochure material v3 revision on September 13, 2026. It is installed in the native CARLA project as `vehicle.lincoln.mkz_interior`. `accepted-build-native.json` records final Blueprint compilation, `accepted-build-manifest.json` records hashes of the saved Blender sources and installed Unreal assets, and `runtime-report.json` records the fresh runtime check.

This revision refines the cabin against pages 3–4 of the 2020 Lincoln MKZ brochure. See the latest runtime reports for capture checks and visual review. Brochure similarity is a visual assessment, separate from successful vehicle integration. This remains an approximation; the stock dashboard texture resolution and simplified trim geometry prevent an exact photographic match.

The current editable assembly is `Lincoln_MKZ_Interior_Brochure.blend`, also saved as `Lincoln_MKZ_Interior_Completed.blend` and `Lincoln_MKZ_Interior_Final.blend`. All contain the latest geometry and packed leather textures.

This revision adds a curved, closed tinted moonroof panel with a watertight painted roof frame and interior headliner surround, rubber seals and sunshade guides; front and rear belt webbing, guides, retractors and buckles; a driver shoulder/lap belt fitted to the temporary mannequin; overhead console/map-light lenses, sun visors and vanity covers; grab handles; rear console vents, USB openings and power cover; and front seatback pockets. The moonroof glass blocks collision queries and physics so rain cannot pass through the panel. Original stock window glazing is included in the Blender assembly.

The driver belt is static geometry fitted to the default seated mannequin; visible slack and an angular upper-shoulder transition remain. It does not yet follow a different seat, body shape, or animated torso. Roof switches, ports, visors and shade are modeled details, not functional controls. The cabin remains a modeled approximation fitted to CARLA stock geometry, not OEM CAD.

## Brochure detail refinement v2

The second brochure pass adds separate center-vent blades and a center divider, tapered side-vent louvers with small adjustment tabs, two inset console lids and a recessed longitudinal pull, dark upholstery insert welts/center bindings, headrest seams and a closed rear-center armrest outline. The satin finish on the new console panels is darker and rougher than the earlier cover.

Both front doors now have round upper grilles, oval lower grilles, padded armrests and window-switch details. The grilles use modeled caps, bezels and a geometric perforation pattern. They are visual components, without audio or switch functionality. `DoorDetail_FL` and `DoorDetail_FR` remain separate in Blender and export to `SM_MKZ_Door_Details_FL.fbx` and `SM_MKZ_Door_Details_FR.fbx`; they are excluded from the baked cabin export. Their Unreal components are children of the matching moving door, with an inverse hinge-position offset and no added collision. The detail FBXs use whole-car coordinates; the inverse offset is derived from the original skeleton heads in `reference/blender-audit.json`, because CARLA moves each parent door component to its hinge at runtime. Do not reset that child offset to zero.

The source backup preceding these edits is `backups/before-brochure-details-v2.blend`. `refine_brochure_details_v2.py` rebuilds the new cabin details, and `refine_brochure_doors_v2.py` rebuilds the door details. Import using `import_brochure_details_v2.py` followed by `import_brochure_doors_v2.py`. All three current Blender assemblies are saved after these changes. The new scripts leave the original CARLA vehicle assets in place and update only the custom interior variant.

`renders/before_details_v2_dashboard.png` and `renders/before_details_v2_cabin.png` preserve the previous comparison views. `brochure-details-v2.json`, `brochure-doors-v2.json` and `brochure-doors-import.json` record geometry and integration. The normal runtime check now captures seven views, including `carla_front_door.png` with the front passenger door open. See the runtime report for the current capture and visual-review outcome.

## Material refinement v3

The third pass changes materials while preserving the detail geometry and the comparison cameras/lighting. The seat leather is deeper ebony with finer roughness variation; perforated inserts now have coordinated color, normal and roughness maps. Walnut veneer is darker and less orange. The stock atlas metal regions have reduced brightness and revised roughness, and the separate satin console/vent trim uses directional normal and roughness textures with anisotropy. These are authored visual approximations of the brochure finishes, not measured OEM material data.

All three current Blender assemblies contain the revised materials and packed textures. `materials-v3-import.json` records the native Unreal update. `renders/before_materials_v3_dashboard.png` and `renders/before_materials_v3_cabin.png` preserve the previous comparison under identical capture settings. The latest runtime reports record visual review of the new captures.

The source/atlas backup is under `backups/before-materials-v3/`. `refine_materials_v3.py` performs the initial source transformation and should only be applied to that preceding source; it is not a repeatable incremental adjustment. `build_material_textures_v3.py` creates the detail maps; `sync_materials_v3.py` packs them into the current assemblies and bakes the atlas; `import_materials_v3.py` updates the native textures/materials. No mesh reimport is needed for this material-only pass because material slots and geometry did not change.

## Assets and integration

- `Lincoln_MKZ_Interior_Completed.blend`: editable assembly, with separate named completion objects and glass.
- `Lincoln_MKZ_Interior_Final.blend`: current compatibility copy.
- `exports/SM_MKZ_Cabin_Final.fbx`: cabin and seats, with earlier belt strips removed.
- `exports/SM_MKZ_Completion_Details.fbx`: additional opaque cabin fittings and unused belts.
- `exports/SM_MKZ_Driver_Worn_Belt.fbx`: separate shoulder/lap webbing for the default mannequin.
- `exports/SM_MKZ_Cabin_Brochure.fbx`: latest cabin and seat geometry.
- `exports/SM_MKZ_Moonroof_Glass.fbx`: separate curved translucent roof panel.
- `exports/SM_MKZ_Moonroof_Frame.fbx`: watertight opaque roof/headliner ring around the bounded glass opening.
- `exports/SM_MKZ_Glass_Without_Moonroof.fbx`: original windshield/other glass with the overlapping roof section removed.
- `Lincoln_MKZ_Glazing_Export.blend`: saved glazing export source with original material slots.
- `roof-overlap-import.json`: saved blueprint replacement and stock glass material verification.
- `completion-import.json`: imported meshes, material assignments and bounds.
- `runtime-report.json`: latest CARLA camera/physics checks and visual review status.
- `renders/carla_*.png`: latest runtime captures, including roof, driver belt and rear console.

Blueprint: `/Game/VehicleInteriors/Lincoln/BP_LincolnMKZ_Interior`.
CARLA vehicle identifier: `vehicle.lincoln.mkz_interior`.
The stock skeleton, physics and exterior rig remain in use. All additional fittings use static mesh components. The active cabin component uses `SM_MKZ_Cabin_Brochure` and `MI_MKZ_Brochure_Interior`, derived from the stock material master. The new finish restores clean center-stack metal pixels, smooths major dashboard panels, rounds headrests, softens seat bolsters and adds leather grain/perforation normal maps plus a closed console cover and padded armrest.

Start a fresh CARLA session to load updated assets, then spawn using:

```bash
/mnt/simulations/bin/carla-python /mnt/simulations/vehicle-interiors/scripts/spawn_lincoln.py --port 2000
```

## Editing and verification

Edit the saved completed Blender assembly directly. Do not rerun the historical `finish_blender_interior.py` script over it: that script rebuilds the earlier incomplete revision. `complete_missing_interior.py` records the initial completion build; `fit_completion_belts.py` records an earlier body fit, superseded by `refine_worn_belts.py` for smooth shoulder and lap webbing. Repeatedly rebuilding from the already-completed file would duplicate details.

`import_completion.py` imports the four current FBX meshes, assigns materials and saves the blueprint. `fix_moonroof_material.py` creates the two-sided tinted instance of the stock vehicle glass shader; retain that assignment when reimporting. `import_roof_overlap_fix.py` imports and assigns the stock-glass replacement. This is required: the original glass mesh contains an overlapping roof surface that suppresses the new transparent panel in the interior view. The replacement preserves both original glass/emissive material slots and removes only 1,364 roof faces. `build_moonroof_frame.py` and `install_moonroof_frame.py` build and install the painted exterior/headliner ring that bounds the glass opening. `enable_moonroof_collision.py` configures the glass as a curved `BlockAll` collision surface; rerun it after any Blueprint or glass-mesh reimport that resets component collision. `verify_carla.py` starts an isolated server on port 2230, captures views sequentially, checks four-wheel physics and vehicle bounds, and shuts down its own session. Set `LINCOLN_ROOF_ONLY=1` to capture the moonroof interior and exterior validation pair. The test uses `NoZenLocalFallback` with the current local cache at `/home/william/.cache/carla-ddc`, and project-default shader compilation. Read `runtime-report.json` for the actual outcome; image capture alone does not imply visual acceptance. Quest testing remains deferred.

## Brochure comparison captures

`renders/brochure_dashboard.png` and `renders/brochure_cabin.png` are actual CARLA RGB sensor images, captured with `render_brochure_carla.py`. The latter opens the front passenger door. The capture helper temporarily hides the mannequin and its worn belt and loads `L_MKZ_Photography` with three soft fill lights for these empty-cabin images. These are presentation conditions; the saved vehicle retains its mannequin and normal runtime lighting.

`LincolnBrochure` and `LincolnInterior` post-process profiles are installed under the project's `Content/Carla/Config/PostProcess` directory; copies are in `reference/`. `LincolnInterior` is used for the six ordinary occupied inspection views. `brochure-runtime-report.json` records the presentation settings and results. `activate_brochure_cabin.py` assigns the current cabin mesh, finish and matching door materials. `import_worn_belt.py` updates only the worn belt mesh.

The current finish and geometry are visibly closer to the ebony brochure reference, but instrument graphics, small controls, door trim shapes and some upholstery details still reflect the simplified stock/procedural assets. Do not interpret a runtime PASS as an exact brochure-match certification.

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
