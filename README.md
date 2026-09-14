# CARLA_CARLab

CARLab's CARLA UE5 simulator source and custom scene/vehicle data. This repository is paired with [UE5_CARLab](https://github.com/Croquembouche/UE5_CARLab) and the [CARLA_MCP WebUI and MCP server](https://github.com/Croquembouche/CARLA_MCP).

The source preserves CARLA's upstream history and includes the workstation changes captured on 11 September 2026. Its upstream base is `1360bb9aff0f1aaa6216876ceee777528128306f` on `ue5-dev`; the required engine is UE 5.5.4. This is a customized research simulator, not an unmodified upstream release.

## Included capabilities

- GPU ray queries for LiDAR, semantic LiDAR and radar, asynchronous camera readback, and multi-GPU sensor workers.
- Multi-GPU routing, sensor lifecycle, weather synchronization and rendering reliability changes.
- Protected/permissive movement signals, recorder/replayer support and Traffic Manager behavior changes.
- Walker navigation and traffic-manager destination fixes.
- Lincoln interior and replaceable occupant components, custom vehicle blueprints, Blender authoring files and cabin camera assets.
- Cleaned Town10 package, removed-scene-vehicle manifest, traffic signal assets and scene-controllable vehicle variants.

The WebUI's physical parking controller, scenario authoring, sensor previews, ROS 2 recording and MCP API are in **CARLA_MCP**. Vehicle physics remains Chaos; GPU ray processing does not mean GPU vehicle physics. Experimental sensor geometry and historical performance caveats are documented in [GPU-SENSORS.md](carlab/GPU-SENSORS.md).

## Start here

Read [SETUP.md](carlab/SETUP.md) for the pinned engine/content setup, applying custom content, compilation, Blender tools and service installation.

Use the [end-to-end installation steps](carlab/SETUP.md), starting with system prerequisites and the writable `/mnt/simulations` layout. Install Git LFS before cloning, use this repository's `main` branch, restore the stock/custom content once, then build both the native dependencies and `CarlaUnrealEditor`. The guide also covers first launch and later rebuilds.

`setup_content.py` fetches the pinned upstream content repository when absent, checks its revision, and overlays the committed custom content. It refuses a mismatched revision or pre-existing local modifications. The upstream asset download is large (the installed content occupies about 82 GB). Custom content is stored separately so an upstream asset checkout cannot hide it from Git.

## Repository layout

| Path | Contents |
|---|---|
| `LibCarla`, `PythonAPI`, `Unreal` | Native simulator, Python bindings and Unreal project sources |
| `carlab/content-overrides` | Modified Town10 map, native interior assets, vehicle and signal blueprints |
| `carlab/config-overrides` | Local Unreal editor/project configuration |
| `carlab/vehicle-interiors` | Blender scenes, FBX exports, scripts, references and development evidence |
| `carlab/maps` | Reusable cleaned Town10 package and cleanup verification |
| `carlab/host-setup` | Environment, launch/build scripts and original installation documentation |
| `carlab/verification` | GPU/camera/runtime verification outputs |
| `carlab/upstream-lock.json` | Exact engine, simulator, content and BlenderTools revisions |

Large new assets are tracked with Git LFS. A normal clone must run `git lfs pull` before using them. Downloaded stock CARLA content is reconstructed from its pinned upstream repository; build outputs, package caches and the workstation's mounted filesystem image are not source artifacts and are not uploaded. Original upstream documentation is retained in [UPSTREAM-README.md](carlab/UPSTREAM-README.md).

## Validation and scope

The captured modifications were built and live-tested on the workstation. Tests and historical measurements are supplied with their original context. The matching WebUI's September 11 parking acceptance completed R135 and R136 physically, with 76 regression tests and geometric paths for all 180 bays; it does not certify driving every bay under live traffic.

## Licensing

Keep the upstream CARLA licenses and asset notices. Third-party Unreal/Blender/CARLA content retains its respective license. This repository is public. Engine-only patches and diagnostic engine backups are maintained in the restricted UE5_CARLab_Source companion. No new license is imposed on upstream material.

## Physical LiDAR and live weather

The native ordinary LiDAR supports an uncalibrated pulsed time-of-flight model, infrared material response, motion-aware ray queries, and extended per-return data in Python and ROS 2. Scene rain and fog update existing sensors each scan. Very dense fog can suppress surface echoes while atmospheric returns remain; the camera uses the same base fog extinction.

See [physical model and point schema](carlab/docs/lidar-physical.md), [live weather](carlab/docs/lidar-weather.md), and [verified runtime results](carlab/verification/lidar-live-weather-20260913/REPORT.md). The content overlays install the required generic profile and material configuration under `Content/Carla/Config/Lidar`. Rebuild the native plugin and Python wheel together before using the extended data format. Use the updated private engine companion for the supporting ray-tracing changes.
