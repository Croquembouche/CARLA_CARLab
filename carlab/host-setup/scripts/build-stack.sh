#!/usr/bin/env bash
set -Eeuo pipefail
source /mnt/simulations/carla/carlab/host-setup/env.sh
export PATH="$CARLA_VENV/bin:$SIMULATIONS_LINUX/bin:/opt/cmake-3.28.3-linux-x86_64/bin:/usr/local/bin:/usr/bin:/bin"
export TMPDIR="$SIMULATIONS_LINUX/tmp"
export PIP_CACHE_DIR="$SIMULATIONS_LINUX/cache/pip"
export CMAKE_BUILD_PARALLEL_LEVEL=24
export NUGET_PACKAGES="$SIMULATIONS_LINUX/cache/nuget"
export DOTNET_CLI_HOME="$SIMULATIONS_LINUX/cache/dotnet"
export GIT_TERMINAL_PROMPT=0
unset PYTHONHOME PYTHONPATH
exec 9>"$SIMULATIONS_LINUX/.build-stack.lock"
flock -n 9 || { echo 'Another build is running'; exit 1; }
status() { printf '%s %s\n' "$(date --iso-8601=seconds)" "$*" | tee "$SIMULATIONS_ROOT/logs/build-status.txt"; }
trap 'status "FAILED at line $LINENO; see build-stack.log"' ERR
# Allow downloads launched during initial installation to complete.
status 'Downloading Unreal dependencies and CARLA assets'
initial_engine_pid="${1:-}"
initial_content_pid="${2:-}"
if [[ -f "$SIMULATIONS_LINUX/.asset-download.pid" ]]; then
  initial_content_pid=$(cat "$SIMULATIONS_LINUX/.asset-download.pid")
fi
if [[ -n "$initial_engine_pid" ]]; then
  while kill -0 "$initial_engine_pid" 2>/dev/null; do sleep 10; done
fi
status 'Checking Unreal dependencies'
cd "$CARLA_UNREAL_ENGINE_PATH"
if [[ ! -f Engine/Build/OneTimeSetupPerformed ]]; then bash Setup.sh --force --threads=12; fi
status 'Generating Unreal project files'
if [[ ! -f Makefile ]]; then bash GenerateProjectFiles.sh; fi
status 'Compiling Unreal Engine 5.5.4'
make -j1 UnrealEditor ShaderCompileWorker UnrealPak
status 'Installing CARLA Python build dependencies'
python -m pip install -r "$CARLA_ROOT/requirements.txt"
status 'Checking CARLA assets'
if [[ -f "$SIMULATIONS_LINUX/.asset-download.pid" ]]; then
  initial_content_pid=$(cat "$SIMULATIONS_LINUX/.asset-download.pid")
fi
if [[ -n "$initial_content_pid" ]]; then
  while kill -0 "$initial_content_pid" 2>/dev/null; do sleep 10; done
fi
git -C "$CARLA_ROOT/Unreal/CarlaUnreal/Content/Carla" lfs pull
while [[ -e "$SIMULATIONS_LINUX/.sensor-source-editing" ]]; do
  status 'Unreal ready; waiting for GPU sensor source edits'
  sleep 10
done
status 'Configuring CARLA UE5 with ROS2'  
cd "$CARLA_ROOT"
cmake -G Ninja -S . -B Build --toolchain="$CARLA_ROOT/CMake/Toolchain.cmake" \
 -DCMAKE_BUILD_TYPE=Release -DENABLE_ROS2=ON \
 -DPython_ROOT_DIR="$CARLA_VENV" -DPython3_ROOT_DIR="$CARLA_VENV" \
 -DPython_EXECUTABLE="$CARLA_VENV/bin/python" -DPython3_EXECUTABLE="$CARLA_VENV/bin/python" \
 -DCARLA_UNREAL_ENGINE_PATH="$CARLA_UNREAL_ENGINE_PATH"
status 'Compiling CARLA and Python API'
cmake --build Build --parallel 24
cmake --build Build --target carla-python-api-install --parallel 24
status 'Checking compiled Python API'
python -c 'import carla; print("CARLA_PYTHON_API_OK", carla.__file__); print(carla.Transform())'
status 'Build complete; verifying CARLA RPC, simulation ticks, and RGB camera'
python "$SIMULATIONS_ROOT/scripts/verify-carla.py"
status 'COMPLETE: Unreal and CARLA compiled; RPC, ticks, and RGB camera verified'

