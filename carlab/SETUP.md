# Build and restore the CARLab stack

This guide is the canonical installation order for all three CARLab components. Commands target **Ubuntu 22.04 x86-64, Python 3.10 and the published `main` branches**. The engine is UE 5.5.4; exact upstream bases and the content/BlenderTools pins are in [upstream-lock.json](upstream-lock.json). Use an NVIDIA GPU with a working Vulkan driver for this GPU sensor deployment. Plan for several hundred GB of free space for sources, stock assets, dependencies and compilation.

These are first-install instructions. On an existing installation, preserve local source/content/configuration changes and use the rebuild section instead of cloning or restoring over them. Build outputs depend on absolute paths: do not move a configured checkout and reuse its old `Build` directory. The original workstation's mounts and services are not installed by cloning these repositories.

## 1. System prerequisites

Install the Linux packages used by the pinned CARLA prerequisite script, plus Python venv support. The package list below is for Ubuntu 22.04. Install and validate the appropriate NVIDIA driver separately; these commands do not select or replace it.

```bash
sudo apt-get update
sudo apt-get install build-essential make ninja-build libvulkan1 \
  libpng-dev libtiff5-dev libjpeg-dev tzdata sed curl libtool rsync \
  libxml2-dev git git-lfs libnss3-dev libatk-bridge2.0-dev \
  libxkbcommon-dev libgbm-dev libpango1.0-dev libasound2-dev \
  libsdl2-dev libfreetype-dev python3.10 python3.10-dev python3.10-venv

git lfs install
nvidia-smi
```

CMake 3.28.3 is installed into the build venv in step 3; Ubuntu 22.04's default CMake is too old. The source reference for the package list is [InstallPrerequisites.sh](../Util/SetupUtils/InstallPrerequisites.sh). The [upstream CARLA build guide](../Docs/build_linux_ue5.md) is background reference: its `CarlaSetup.sh` flow downloads upstream sources and is not the restore procedure for this customized snapshot.

## 2. Layout, source access and assets

Obtain [Epic GitHub source access](https://www.unrealengine.com/ue-on-github) and confirm access to [UE5_CARLab_Source](https://github.com/Croquembouche/UE5_CARLab_Source) before cloning. Authenticate through Git's credential helper; do not put tokens into clone URLs. A private-repository 404 means the signed-in account has not been granted access.

The launchers and WebUI currently expect `/mnt/simulations`. Use a writable Linux filesystem with executable files and symlink support. A normal ext4 directory/filesystem works; a loop image is not required. If the directory does not exist, create it as follows. If it is already a mount, make sure the intended storage is mounted and writable first.

```bash
if [ ! -d /mnt/simulations ]; then
  sudo install -d -o "$(id -un)" -g "$(id -gn)" /mnt/simulations
fi
test -w /mnt/simulations

git clone --branch main https://github.com/Croquembouche/UE5_CARLab_Source.git /mnt/simulations/UnrealEngine5_carla
git clone --branch main https://github.com/Croquembouche/CARLA_CARLab.git /mnt/simulations/carla
git clone --branch main https://github.com/Croquembouche/CARLA_MCP.git /mnt/simulations/control-center

git -C /mnt/simulations/carla lfs pull
git -C /mnt/simulations/control-center lfs pull
cd /mnt/simulations/carla
python3.10 carlab/setup_content.py
```

The content tool clones the pinned stock checkout, downloads its LFS objects, then applies the custom native content and configuration. Stock content occupied about 82 GB on the publication workstation. The tool refuses a wrong revision, local stock modifications, or unexpanded custom LFS pointers. Once the overlay has been applied, the stock checkout is intentionally modified: **do not rerun this first-install step during a rebuild**. Preserve it rather than resetting its changes. The cleaned map is already part of the overlay; applying its standalone package again is unnecessary.

## 3. Environment and build dependencies

Run in the same Bash terminal for the rest of the build:

```bash
cd /mnt/simulations/carla
mkdir -p /mnt/simulations/{bin,cache,tmp,venvs,verification}
mkdir -p carlab/host-setup/logs
install -m 755 carlab/host-setup/bin/* /mnt/simulations/bin/
unset PYTHONHOME PYTHONPATH
python3.10 -m venv /mnt/simulations/venvs/carla
source carlab/host-setup/env.sh
source "$CARLA_VENV/bin/activate"
python -m pip install --upgrade pip
python -m pip install -r requirements.txt 'numpy==1.26.4' 'cmake==3.28.3'
cmake --version
export BUILD_JOBS=8
```

`BUILD_JOBS` controls the commands below; reduce it if RAM is limited. The original workstation used 24 actions with more memory. `make -j1` below limits the outer Make invocation; UnrealBuildTool also makes its own parallelism decisions. `source carlab/host-setup/env.sh` is required in each new shell that uses the launchers/build variables; cloning does not edit your shell startup files.

## 4. Compile the matched engine and simulator

```bash
cd "$CARLA_UNREAL_ENGINE_PATH"
bash Setup.sh
bash GenerateProjectFiles.sh
make -j1 UnrealEditor ShaderCompileWorker UnrealPak

cd "$CARLA_ROOT"
cmake -G Ninja -S . -B Build --toolchain="$CARLA_ROOT/CMake/Toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_ROS2=ON \
  -DCARLA_UNREAL_ENGINE_PATH="$CARLA_UNREAL_ENGINE_PATH" \
  -DPython_ROOT_DIR="$CARLA_VENV" -DPython3_ROOT_DIR="$CARLA_VENV" \
  -DPython_EXECUTABLE="$CARLA_VENV/bin/python" -DPython3_EXECUTABLE="$CARLA_VENV/bin/python"
cmake --build Build --parallel "$BUILD_JOBS"
cmake --build Build --target carla-python-api-install --parallel "$BUILD_JOBS"
cmake --build Build --target carla-unreal-configure --parallel "$BUILD_JOBS"
"$CARLA_UNREAL_ENGINE_PATH/Engine/Build/BatchFiles/Linux/Build.sh" \
  CarlaUnrealEditor Linux Development \
  -project="$CARLA_ROOT/Unreal/CarlaUnreal/CarlaUnreal.uproject" \
  -buildscw -MaxParallelActions="$BUILD_JOBS"
"$CARLA_VENV/bin/python" -c 'import carla; print(carla.__file__); print(carla.Transform())'
test -x "$CARLA_UNREAL_ENGINE_PATH/Engine/Binaries/Linux/UnrealEditor"
test -f "$CARLA_ROOT/Unreal/CarlaUnreal/Binaries/Linux/libUnrealEditor-CarlaUnreal.so"
test -f "$CARLA_ROOT/Unreal/CarlaUnreal/Plugins/Carla/Binaries/Linux/libUnrealEditor-Carla.so"
```

**The explicit `CarlaUnrealEditor` build is required.** CMake's default target and Python API installation do not by themselves build the editor application/module used by `carla-sim`. The toolchain uses the Unreal clang/sysroot downloaded by `Setup.sh`. Do not substitute a generic system clang or install the upstream PyPI `carla` wheel for these custom native bindings.

The `ENABLE_ROS2` CMake option builds CARLA's native ROS support. The WebUI additionally needs the ROS 2 Humble Python/message packages described in its installation guide.

## 5. WebUI, MCP and first launch

Continue with [CARLA_MCP installation](https://github.com/Croquembouche/CARLA_MCP/blob/main/docs/SETUP.md). That guide installs ROS Python packages, Node dependencies, the two application venvs, and optional user services. Launch the WebUI and use its Start action to start CARLA; the WebUI owns synchronous stepping. Do not run a separate `carla-multigpu` owner alongside it on the same ports.

For a native-only smoke test, with sufficient free GPU memory and ports 2200–2202 unused:

```bash
source /mnt/simulations/carla/carlab/host-setup/env.sh
"$CARLA_VENV/bin/python" "$SIMULATIONS_ROOT/scripts/verify-carla.py" --skip-build
```

This launches an isolated simulator, creates a vehicle/RGB camera, ticks it and stops its own process. It writes current results under `/mnt/simulations/verification`. Run it only when the GPU resources are available; it is not a read-only status check. Historical [GPU sensor measurements](GPU-SENSORS.md) are supplied separately.

## Rebuilding an existing configured installation

Preserve the content overlay and saved application data. After checking out compatible code changes, source the environment and run:

```bash
source /mnt/simulations/carla/carlab/host-setup/env.sh
BUILD_JOBS=8 bash "$SIMULATIONS_ROOT/scripts/build-stack.sh"
```

This builds the engine, C++ dependencies, Python API and Unreal application module. It does not start a simulator unless `--verify-runtime` is supplied. Do not apply the archived patches to the already-modified source. Older diagnostic scripts are retained as evidence and may have workload-specific assumptions; the commands in this guide are the supported setup path.

## Optional: Blender and custom interior authoring

Blender is not needed to run the committed Unreal assets. For authoring, download Blender 5.2.1 Linux x64 from the [official 5.2 release directory](https://download.blender.org/release/Blender5.2/), verify the archive with its published SHA-256 file, and extract it to `/mnt/simulations/blender-5.2.1-linux-x64`.

On a fresh installation:

```bash
source /mnt/simulations/carla/carlab/host-setup/env.sh
git clone https://github.com/EpicGamesExt/BlenderTools.git "$BLENDER_TOOLS_ROOT"
git -C "$BLENDER_TOOLS_ROOT" checkout 04cae45d71874a37d75522f4a694a5d4aae32147
git -C "$BLENDER_TOOLS_ROOT" apply --check "$SIMULATIONS_ROOT/scripts/blender-tools-local-compatibility.patch"
git -C "$BLENDER_TOOLS_ROOT" apply "$SIMULATIONS_ROOT/scripts/blender-tools-local-compatibility.patch"
mkdir -p "$BLENDER_USER_RESOURCES/scripts/addons"
ln -s "$BLENDER_TOOLS_ROOT/send2ue" "$BLENDER_USER_RESOURCES/scripts/addons/send2ue"
ln -s "$BLENDER_TOOLS_ROOT/ue2rigify" "$BLENDER_USER_RESOURCES/scripts/addons/ue2rigify"
ln -s "$CARLA_ROOT/carlab/vehicle-interiors" /mnt/simulations/vehicle-interiors
"$BLENDER_BIN" --background --factory-startup --threads 8 \
  --python "$SIMULATIONS_ROOT/scripts/verify-blender.py"
blender-carla /mnt/simulations/vehicle-interiors/Lincoln_MKZ_Interior_Final.blend
```

The symlink preserves the absolute authoring paths used by the included scripts. If a destination already exists, inspect and preserve it rather than replacing it. The verification script enables Rigify/Send to Unreal/UE to Rigify and saves preferences in the dedicated Blender user-resources directory. Those preferences are created locally; a `blender-config` snapshot is not included in Git. The compatibility patch should be applied only once to the clean pinned BlenderTools revision.

See [the interior README](vehicle-interiors/README.md) for the final model and historical prototype steps. The native interior assets are already in the content overlay, so do not reimport them just to run the simulator.

## What has been validated

The publication host previously built and exercised the custom engine and CARLA. The setup audit checks command paths/targets, fresh Python and Node installation, application imports/tests, services, content restoration with a small fixture, and documentation links. It is not a clean-machine full-engine rebuild or an 82 GB stock-content redownload. Follow the runtime checks on each target machine before treating that installation as ready.

The small content-restoration regression tests use temporary local Git repositories and do not download stock assets or touch an installed simulator:

```bash
cd /mnt/simulations/carla
python3.10 carlab/tests/test_setup_content.py
```
