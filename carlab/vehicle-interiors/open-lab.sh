#!/usr/bin/env bash
set -euo pipefail
exec /mnt/simulations/bin/carla-editor /Game/VehicleInteriors/Lincoln/L_LincolnInteriorLab -ddc=LincolnInteriorDDC -ini:Engine:[DevOptions.Shaders]:bAllowCompilingThroughWorkers=False "$@"
