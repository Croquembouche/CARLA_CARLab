#!/usr/bin/env bash
set -Eeuo pipefail
source /mnt/simulations/carla/carlab/host-setup/env.sh
export GIT_TERMINAL_PROMPT=0 GIT_LFS_FORCE_PROGRESS=1
unset GIT_LFS_PROGRESS  # Per-chunk fsync of this log throttles large asset transfers.
echo $$ > "$SIMULATIONS_LINUX/.asset-download.pid"
cd "$CARLA_ROOT/Unreal/CarlaUnreal/Content/Carla"
# Restore the fresh checkout's index without needlessly rehashing 40 GB of assets.
git read-tree --reset HEAD
"$SIMULATIONS_LINUX/bin/git-lfs" pull
printf '%s ASSETS_COMPLETE\n' "$(date --iso-8601=seconds)"
