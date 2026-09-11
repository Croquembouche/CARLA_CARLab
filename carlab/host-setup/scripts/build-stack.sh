#!/usr/bin/env bash
set -Eeuo pipefail
source /mnt/simulations/carla/carlab/host-setup/env.sh
verify_runtime=0
if [[ "${1:-}" == --verify-runtime && $# == 1 ]]; then
  verify_runtime=1
elif [[ $# != 0 ]]; then
  echo 'Usage: build-stack.sh [--verify-runtime]' >&2; exit 2
fi
export PATH="$CARLA_VENV/bin:$SIMULATIONS_LINUX/bin:$PATH"
export BUILD_JOBS="${BUILD_JOBS:-8}"
[[ "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'BUILD_JOBS must be a positive integer' >&2; exit 2; }
export TMPDIR="$SIMULATIONS_LINUX/tmp"
export PIP_CACHE_DIR="$SIMULATIONS_LINUX/cache/pip"
export NUGET_PACKAGES="$SIMULATIONS_LINUX/cache/nuget"
export DOTNET_CLI_HOME="$SIMULATIONS_LINUX/cache/dotnet"
unset PYTHONHOME PYTHONPATH
[[ -x "$CARLA_VENV/bin/python" ]] || { echo 'Create the CARLA build venv using carlab/SETUP.md first' >&2; exit 1; }
[[ -d "$CARLA_ROOT/Unreal/CarlaUnreal/Content/Carla/.git" ]] || { echo 'Restore content using carlab/SETUP.md first' >&2; exit 1; }
mkdir -p "$TMPDIR" "$PIP_CACHE_DIR" "$NUGET_PACKAGES" "$DOTNET_CLI_HOME" "$SIMULATIONS_ROOT/logs" "$SIMULATIONS_LINUX/verification"
exec 9>"$SIMULATIONS_LINUX/.build-stack.lock"
flock -n 9 || { echo 'Another build is running'; exit 1; }
status() { printf '%s %s\n' "$(date --iso-8601=seconds)" "$*" | tee "$SIMULATIONS_ROOT/logs/build-status.txt"; }
trap 'status "FAILED at line $LINENO"' ERR
status 'Installing build dependencies'
python -m pip install -r "$CARLA_ROOT/requirements.txt" 'numpy==1.26.4' 'cmake==3.28.3'
cd "$CARLA_UNREAL_ENGINE_PATH"
status 'Checking Unreal dependencies'
if [[ ! -f Engine/Build/OneTimeSetupPerformed ]]; then bash Setup.sh; fi
if [[ ! -f Makefile ]]; then bash GenerateProjectFiles.sh; fi
status 'Compiling Unreal Engine'
make -j1 UnrealEditor ShaderCompileWorker UnrealPak
status 'Configuring CARLA'
cd "$CARLA_ROOT"
cmake -G Ninja -S . -B Build --toolchain="$CARLA_ROOT/CMake/Toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_ROS2=ON \
  -DPython_ROOT_DIR="$CARLA_VENV" -DPython3_ROOT_DIR="$CARLA_VENV" \
  -DPython_EXECUTABLE="$CARLA_VENV/bin/python" -DPython3_EXECUTABLE="$CARLA_VENV/bin/python" \
  -DCARLA_UNREAL_ENGINE_PATH="$CARLA_UNREAL_ENGINE_PATH"
status 'Compiling native dependencies and Python API'
cmake --build Build --parallel "$BUILD_JOBS"
cmake --build Build --target carla-python-api-install --parallel "$BUILD_JOBS"
cmake --build Build --target carla-unreal-configure --parallel "$BUILD_JOBS"
status 'Compiling the CARLA Unreal application module'
"$CARLA_UNREAL_ENGINE_PATH/Engine/Build/BatchFiles/Linux/Build.sh" \
  CarlaUnrealEditor Linux Development \
  -project="$CARLA_ROOT/Unreal/CarlaUnreal/CarlaUnreal.uproject" \
  -buildscw -MaxParallelActions="$BUILD_JOBS"
python -c 'import carla; print("CARLA_PYTHON_API_OK", carla.__file__); print(carla.Transform())'
status 'Build complete'
if [[ "$verify_runtime" == 1 ]]; then
  python "$SIMULATIONS_ROOT/scripts/verify-carla.py" --skip-build
  status 'Build and isolated runtime verification complete'
fi
