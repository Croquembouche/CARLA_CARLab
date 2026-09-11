#!/usr/bin/env bash
set -Eeuo pipefail
source /mnt/simulations/carla/carlab/host-setup/env.sh
export PATH="$CARLA_VENV/bin:$SIMULATIONS_LINUX/bin:/opt/cmake-3.28.3-linux-x86_64/bin:/usr/local/bin:/usr/bin:/bin"
export TMPDIR="$SIMULATIONS_LINUX/tmp"
export NUGET_PACKAGES="$SIMULATIONS_LINUX/cache/nuget"
export DOTNET_CLI_HOME="$SIMULATIONS_LINUX/cache/dotnet"
unset PYTHONHOME PYTHONPATH
# The main build owns this exact make process. Wait for all three engine targets.
while kill -0 290558 2>/dev/null; do sleep 5; done
printf '%s Compiling CARLA while the asset download completes\n' "$(date --iso-8601=seconds)"
cmake --build "$CARLA_ROOT/Build" --target carla-unreal-configure libsqlite3
"$UE5_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" CarlaUnrealEditor Linux Development \
 -project="$CARLA_ROOT/Unreal/CarlaUnreal/CarlaUnreal.uproject" -buildscw -WaitMutex
rm /mnt/simulations/.sensor-source-editing
printf '%s CARLA_EDITOR_COMPILE_COMPLETE\n' "$(date --iso-8601=seconds)"
