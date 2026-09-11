# CARLab host launchers and build helpers

Use [the complete setup guide](../SETUP.md) for first installation. These files are the published launcher sources; cloning does not install commands, configure mounts, or enable services.

After installation at `/mnt/simulations/carla`, source:

```bash
source /mnt/simulations/carla/carlab/host-setup/env.sh
```

`SIMULATIONS_ROOT` points to this `host-setup` directory; `SIMULATIONS_LINUX` is `/mnt/simulations`. `CARLA_ROOT`, `CARLA_UNREAL_ENGINE_PATH`, `CARLA_VENV` and the Blender variables name their sibling installations under `/mnt/simulations`. The setup guide installs `bin/*` into `/mnt/simulations/bin` and creates writable runtime directories.

| Command | Purpose |
|---|---|
| `carla-build` | Build the configured engine, CARLA native dependencies, Python API and Unreal application |
| `carla-sim -RenderOffScreen` | Launch the CARLA Unreal project as a game |
| `carla-editor` | Open the CARLA project in UnrealEditor |
| `carla-multigpu --dry-run` | Print the worker launch commands without starting CARLA |
| `carla-python script.py` | Run Python with the custom native CARLA module |
| `carla_activate` | Activate the CARLA build venv in the current shell |
| `blender-carla` | Open the optional pinned Blender installation with its separate preferences |
| `simulations-status` | Read local build/service status if present |
| `carla-web-status` | Read the optional WebUI user-service status |

Run the WebUI's Start action for normal integrated use; it owns synchronous stepping and starts workers on demand. Standalone launch/verification commands are for a separate session with available ports/GPU resources.

[GPU-SENSORS.md](GPU-SENSORS.md) contains historical measurements and experimental limits. `installation.json` records the original workstation and is not a status report for a fresh clone. Runtime logs are created in `logs/`; new verification results belong under `/mnt/simulations/verification`. Published verification evidence is in [../verification](../verification).

The source workstation used an ext4 image on an NTFS disk. A new installation can use a normal writable Linux filesystem; it does not need that image or the workstation's fstab/storage-tuning service. See [CONTROL-CENTER.md](CONTROL-CENTER.md) for the WebUI companion.
