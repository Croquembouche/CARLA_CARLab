# Synchronized update latency repair

Use the matching [private engine source](https://github.com/Croquembouche/UE5_CARLab_Source) containing the light-function preparation and Chaos collision-filter repairs. Rebuild the engine and CARLA editor target before enabling these options.

Set these environment variables in the process or service that launches `carla-multigpu.py`:

```sh
export CARLA_PRECACHE_LIGHT_FUNCTIONS=1
export CARLA_SCOPED_MISS_SHADERS=1
export CARLA_REUSE_COMPLETE_PIPELINE=1
```

Restart the workers after saving any active simulation state. The launcher applies light-function preparation through early Engine SystemSettings as well as ExecCmds: secondary vehicle components register before ExecCmds run. Preparation is supported in the uncooked editor runtime. Options remain disabled by default. `CARLA_PIPELINE_COVERAGE_AUDIT=1` enables diagnostic shader logging; leave it unset for normal operation.

The companion engine change prepares indicator light-function shaders before capture, retains exact shader coverage when reusing ray-tracing pipelines, and rejects unsupported triangle-mesh/triangle-mesh contact candidates before the assertion path. Supported contacts and sensor scene queries remain unchanged.

Deployed Town02_Opt validation (2026-09-27): six 1280x800 cameras, physical/material 64-channel LiDAR configured for 150 m, ten moving background vehicles, synchronous 0.1 s simulation steps, full raw/ROS recording. Median/p95/p99/max changed from 251/302/7860/7946 ms to 249/282/428/562 ms. All 2107 samples across 301 final updates passed synchronization and payload checks. Three updates still exceeded 500 ms; recorded throughput was 3.83 Hz, so the 10 Hz real-time target remains unmet.

These measurements cover the deployed stack, including earlier local optimizations. This focused launcher commit publishes only the related opt-in settings and early initialization, preserving existing default commands and installation paths. It is not a fresh clean-clone performance benchmark. Detailed evidence is in the engine repository under `carlab/verification/latency-tail-20260927/`.
