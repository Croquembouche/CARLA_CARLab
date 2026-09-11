#!/usr/bin/env bash
set -Eeuo pipefail
source /mnt/simulations/carla/carlab/host-setup/env.sh
status() { printf '%s %s\n' "$(date --iso-8601=seconds)" "$*" | tee "$SIMULATIONS_ROOT/logs/build-status.txt"; }
verification_log=runtime-verification.log
if [[ "${1:-}" == --gpu-only ]]; then verification_log=gpu-verification.log; fi
trap 'status "FAILED: runtime verification; see $verification_log"' ERR
if [[ "${1:-}" == --gpu-only ]]; then
  status "Base runtime verified; validating GPU sensors and four-GPU rendering"
  carla-python "$SIMULATIONS_ROOT/scripts/verify-gpu-stack.py" "${@:2}"
else
  carla-python "$SIMULATIONS_ROOT/scripts/verify-carla.py" --skip-build
fi
status 'COMPLETE: installation, camera streams, GPU ray sensors and four-GPU runtime verified'
