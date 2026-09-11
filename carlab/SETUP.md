# Build and restore the CARLab stack

The validated native environment is Ubuntu 22.04, Python 3.10, ROS 2 Humble, NVIDIA Vulkan-capable GPUs, CMake 3.28.3 and the exact engine/content revisions in `upstream-lock.json`. Install Unreal/CARLA Linux prerequisites using the pinned upstream documentation. This guide does not install or replace GPU drivers automatically.

## Layout and sources

Prepare a Linux filesystem at `/mnt/simulations` with sufficient free space. The old workstation uses a mounted ext4 image; a normal ext4 filesystem also works and no disk image is required. Obtain Epic GitHub source access before cloning the restricted engine.

```bash
git clone https://github.com/Croquembouche/UE5_CARLab_Source.git /mnt/simulations/UnrealEngine5_carla
git clone https://github.com/Croquembouche/CARLA_CARLab.git /mnt/simulations/carla
git clone https://github.com/Croquembouche/CARLA_MCP.git /mnt/simulations/control-center
cd /mnt/simulations/carla
git lfs install --local
git lfs pull
python3 carlab/setup_content.py
```

The content tool requires a clean pinned stock checkout; it overlays the cleaned Town10 map, custom native assets and configuration. It does not blindly overwrite an existing modified asset checkout. The standalone cleaned package and original backup are also retained in `carlab/maps`.

## Environment and compilation

```bash
mkdir -p /mnt/simulations/{bin,cache,tmp,venvs}
cp carlab/host-setup/bin/* /mnt/simulations/bin/
python3.10 -m venv /mnt/simulations/venvs/carla
source carlab/host-setup/env.sh
mkdir -p "$SIMULATIONS_ROOT/logs"
cd "$CARLA_UNREAL_ENGINE_PATH"
bash Setup.sh
bash GenerateProjectFiles.sh
make -j1 UnrealEditor ShaderCompileWorker UnrealPak
cd "$CARLA_ROOT"
"$CARLA_VENV/bin/python" -m pip install -r requirements.txt
cmake -G Ninja -S . -B Build --toolchain="$CARLA_ROOT/CMake/Toolchain.cmake" \
 -DCMAKE_BUILD_TYPE=Release -DENABLE_ROS2=ON \
 -DCARLA_UNREAL_ENGINE_PATH="$CARLA_UNREAL_ENGINE_PATH" \
 -DPython_ROOT_DIR="$CARLA_VENV" -DPython3_ROOT_DIR="$CARLA_VENV" \
 -DPython_EXECUTABLE="$CARLA_VENV/bin/python" -DPython3_EXECUTABLE="$CARLA_VENV/bin/python"
cmake --build Build --parallel 24
cmake --build Build --target carla-python-api-install --parallel 24
"$CARLA_VENV/bin/python" -c 'import carla; print(carla.__file__)'
```

The historical `host-setup/scripts/build-stack.sh` is also included, with the original host prefix relocated to this repository's setup directory. Review resource limits and dependency paths before using it on a different machine. Do not apply the archived patch files to this already-modified checkout; they are historical evidence.

## Blender and custom interiors

Download Blender 5.2.1 for Linux from Blender's official distribution, verify its published checksum, and extract it to `/mnt/simulations/blender-5.2.1-linux-x64`. Clone `EpicGamesExt/BlenderTools` into `/mnt/simulations/BlenderTools` and check out the revision in `upstream-lock.json`. Apply `host-setup/scripts/blender-tools-local-compatibility.patch` to that pinned clean checkout. The verification/setup scripts and Blender configuration sources are supplied under `host-setup` and `blender-config`. Do not reimport interiors just to run the simulator: the compiled Unreal assets are already in the content overlay. `vehicle-interiors` retains Blender authoring scenes, FBX exports and import scripts for editing.

## WebUI and MCP

In `/mnt/simulations/control-center`, run `git lfs pull` and `bash scripts/setup-python.sh all`; follow that repository's `docs/SETUP.md` to run the UI or install user services. The launcher starts rendering workers on demand by default (`--gpus auto`); four workers can be explicitly selected for a sensor workload. The UI owns synchronous stepping.

## Included versus reconstructed

The repository contains changed source files, custom data, source assets, setup/launch scripts and verification outputs. Stock content, Unreal dependency downloads, Blender binaries, Python/node environments, compiled engine/CARLA binaries, caches and the host's sparse filesystem image are reconstructed or installed separately. Those are not silently treated as Git source. Each repository includes a publication manifest of the custom data it actually contains.
