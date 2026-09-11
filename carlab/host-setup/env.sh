# Source this file to use the Simulations toolchain.
export SIMULATIONS_ROOT=/mnt/simulations/carla/carlab/host-setup
export SIMULATIONS_LINUX=/mnt/simulations
export CARLA_ROOT="$SIMULATIONS_LINUX/carla"
export CARLA_PATH="$CARLA_ROOT"
export CARLA_UNREAL_ENGINE_PATH="$SIMULATIONS_LINUX/UnrealEngine5_carla"
export UE5_ROOT="$CARLA_UNREAL_ENGINE_PATH"
export UE_ROOT="$CARLA_UNREAL_ENGINE_PATH"
export BLENDER_ROOT="$SIMULATIONS_LINUX/blender-5.2.1-linux-x64"
export BLENDER_BIN="$BLENDER_ROOT/blender"
export BLENDER_TOOLS_ROOT="$SIMULATIONS_LINUX/BlenderTools"
export BLENDER_USER_RESOURCES="$SIMULATIONS_LINUX/blender-config/5.2"
export CARLA_VENV="$SIMULATIONS_LINUX/venvs/carla"

case ":$PATH:" in
  *":$SIMULATIONS_LINUX/bin:"*) ;;
  *) export PATH="$SIMULATIONS_LINUX/bin:$PATH" ;;
esac
carla_activate() { . "$CARLA_VENV/bin/activate"; }

# Clear the obsolete UE4 path inherited from older login sessions.
if [ "${UE4_ROOT:-}" = "$HOME/UnrealEngine_4.26" ] && [ ! -d "$UE4_ROOT" ]; then
  unset UE4_ROOT
fi

# Unreal uses underscore spellings for these variables on Linux.
export UE_LocalDataCachePath="$SIMULATIONS_LINUX/cache/unreal"
export UE_ZenDataPath="$SIMULATIONS_LINUX/cache/zen"
