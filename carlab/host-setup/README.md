# Simulations: CARLA UE5, Unreal Engine, and Blender

Installed from fresh upstream downloads on 2026-09-07. The old `CarlaUE5` partial build and Blender 4.1.1 folders were removed at the user's request.

## Current state

Blender, Unreal, and CARLA are installed and verified. Compilation passed, and the CARLA runtime passed RPC, simulation tick, actor spawning, and RGB camera checks. See `linux/verification/carla-runtime.json` and `logs/build-status.txt`.

```bash
source /media/william/mist1/Simulations/env.sh
simulations-status
# Detailed compilation log:
tail -f /media/william/mist1/Simulations/logs/build-stack.log
```

## Versions

- CARLA: current `ue5-dev`, commit `1360bb9aff0f1aaa6216876ceee777528128306f` (2026-09-02). The UE5 release line is 0.10.0; GitHub's separate 0.9.16 release uses UE4.
- Unreal: CARLA's required `ue5-dev-carla` fork, UE **5.5.4**, commit `2ac0528831e08e80784df2759db9a2c592d3bd4d`. A generic newer Epic engine does not replace this required fork.
- Blender: **5.2.1 LTS**, official Linux archive, verified against Blender's published SHA-256 list.
- Epic BlenderTools: current main, commit `04cae45` (2026-09-03); Send to Unreal 2.4.3 and UE to Rigify 1.6.2, plus Blender's bundled Rigify.

## GPU sensor optimization

The local Vulkan GPU ray backend, asynchronous camera readback changes, and four-GPU launcher are compiled and live validated. A matched Town10HD test measured **88.7% less server CPU work** with four GPU workers and **2.92x throughput** compared with four workers using CPU rays. All six camera stream types and LiDAR/radar checks passed. See [GPU-SENSORS.md](GPU-SENSORS.md) for measured results, launch commands and the experimental render-versus-collision geometry limitations. The backend remains opt-in; the validation does not certify geometry equivalence or long moving runs.

## Environment and commands

`env.sh` is loaded by `~/.bashrc` and `~/.profile`. Existing terminals should source it once.

| Variable | Location under this folder |
|---|---|
| `SIMULATIONS_ROOT` | This folder |
| `SIMULATIONS_LINUX` | `/mnt/simulations`, a fast bind mount of `linux` |
| `CARLA_ROOT`, `CARLA_PATH` | `linux/carla` |
| `CARLA_UNREAL_ENGINE_PATH`, `UE5_ROOT`, `UE_ROOT` | `linux/UnrealEngine5_carla` |
| `CARLA_VENV` | `linux/venvs/carla` |
| `BLENDER_ROOT` | `linux/blender-5.2.1-linux-x64` |
| `BLENDER_BIN` | `linux/blender-5.2.1-linux-x64/blender` |
| `BLENDER_TOOLS_ROOT` | `linux/BlenderTools` |

```bash
blender-carla             # Latest Blender with the configured Epic add-ons
ue5-editor               # Unreal editor
carla-editor             # Open the CARLA Unreal project
carla-sim -RenderOffScreen # Run the CARLA project as a game
carla-python my_script.py # CARLA's isolated Python environment
carla_activate           # Activate that Python environment in this shell
```

Blender and CARLA editor entries are also in the desktop application menu.

## Storage

The parent drive uses NTFS. `simulations.ext4` is a sparse 1 TiB Linux filesystem image mounted at `linux`; it grows physically as files are written. All sources, build artifacts, Blender settings, and the Unreal derived-data cache are on this drive. The mount is registered in `/etc/fstab`, with a dependency on the parent drive and non-blocking boot behavior. The previous fstab was saved as `/etc/fstab.before-simulations-20260907`. A root-owned `simulations-storage-tuning.service` enables direct I/O and disables duplicate loop-level writeback throttling for this image whenever it mounts; its settings apply only to this image, not other disks.

Do not delete, move, or copy `simulations.ext4` while mounted. The `linux` folder is the usable mounted filesystem. `/mnt/simulations` is a second mount of the exact same data; build commands use this alias to avoid repeated NTFS/FUSE parent-directory permission lookups. It uses no extra disk space and does not move the installation onto the system NVMe. Keep the drive mounted while builds or editors run.

## Build and local adjustments

The pipeline is `scripts/build-stack.sh`. It builds Unreal, CARLA, the native ROS2 integration, and the Python API. Unreal compilation is limited to 24 parallel actions. The job is a user service named `simulations-build`; compilation can be resumed by running the script again. A failed stage is recorded in `logs/build-status.txt`.

```bash
bash /media/william/mist1/Simulations/scripts/build-stack.sh
```

The CARLA project enables Python remote execution on loopback for Send to Unreal. The project already includes the required Python, editor scripting, and groom plugins. Send to Unreal expects the CARLA editor to be running; use Blender's **Pipeline > Export > Send to Unreal** menu.

Two local BlenderTools compatibility changes keep the UE to Rigify extension from replacing the installed add-on's Python module and avoid reloading registered FBX classes. The reviewable diff is `scripts/blender-tools-local-compatibility.patch`.

## Verification

- Blender archive SHA-256: passed.
- Blender 5.2.1 startup: passed.
- Rigify, Send to Unreal, UE to Rigify registration and restart persistence: passed.
- Send to Unreal FBX cube export and Cycles render: passed. Outputs are in `linux/verification`.
- NVIDIA OptiX GPU render on RTX 2080 Ti: passed (`linux/verification/blender-optix.png`).
- CARLA/Unreal compile and simulation runtime: passed; details in `linux/verification/carla-runtime.json`.

Upstream references: [CARLA UE5 build instructions](https://carla-ue5.readthedocs.io/en/latest/build_linux_ue5/), [CARLA source](https://github.com/carla-simulator/carla/tree/ue5-dev), [Blender downloads](https://www.blender.org/download/), [Epic BlenderTools](https://github.com/EpicGamesExt/BlenderTools).

The UE launchers clear inherited Python paths and direct Zen derived data to `/mnt/simulations/cache/zen` using `UE_ZenDataPath`. The new first-launch cache was relocated there after its owning processes stopped. The retired `UE4_ROOT` export pointing to the missing `~/UnrealEngine_4.26` directory was removed; `.bashrc.before-retired-ue4-env-20260907` preserves the prior shell configuration.

## Network control center

The separate web interface is available at [128.175.213.232:8095](http://128.175.213.232:8095),
with map-based scenario editing, sensor loadouts, weather, recording/replay and ROS 2 bags.
See [CONTROL-CENTER.md](CONTROL-CENTER.md) for the application and documentation links.
