# GPU sensor work

These are historical measurements from the publication workstation. For first installation, follow [the current stack setup](https://github.com/Croquembouche/CARLA_CARLab/blob/main/carlab/SETUP.md). Benchmark commands below start workloads and require available GPUs/ports.

Status: **compiled and live validated on all four RTX 2080 Ti GPUs** on
2026-09-07. The hardware ray shader passes controlled geometry checks in both
Entry and Town10HD. All six camera stream types pass live checks. Four workers
pass RGB, LiDAR and radar frame/timestamp checks together, followed by an
eight-frame semantic LiDAR check. The verifier confirmed one worker on each of
four distinct physical GPUs and a 2000 MiB texture pool on every worker.

## Measured results

Matched Town10HD_Opt runs use High camera quality, 1280x720 RGB, 64-channel
LiDAR at 1 million requested points/second, and radar at 50,000 requested
points/second; both ray sensors have 100 m range. Single-worker runs have one
of each sensor, and four-worker runs have four of each. Each measurement covers
100 synchronized frames after 30 warmup frames at a 0.05 s simulation step.
The vehicle is stationary; these are short workload measurements, not a long
moving-scene endurance test or a guarantee for every map.

| Layout | Ray backend | Wall-clock FPS | Average server CPU cores | CPU seconds per simulated second |
|---|---|---:|---:|---:|
| Single worker | CPU | 12.02 | 10.53 | 17.516 |
| Single worker | GPU | 11.01 | 2.71 | 4.920 |
| Four workers + primary | CPU | 1.10 | 23.18 | 420.610 |
| Four workers + primary | GPU | 3.22 | 7.66 | 47.604 |

GPU rays reduced CPU work per simulated second by **71.9%** with one worker and
**88.7%** with four workers. Four-worker throughput increased **2.92x**. The
single-worker run was slightly slower in wall time despite its lower CPU cost.
The four-worker workload has four times as many sensors, so its FPS should not
be treated as a scaling comparison against the single-worker workload.
Both baselines already contain the camera readback optimization; these numbers
do not measure the camera change independently. CPU accounting includes all
threads of the supplied server processes and excludes the Python client and
shader compiler child processes; those compiler children were idle when timing
started. GPU utilization and VRAM are sampled, not continuous measurements.

Results: [combined report](../verification/gpu-stack-verification.json),
[four-GPU samples](../verification/sensors-four-gpu.json),
[example camera image](../verification/sensors-four-gpu.png).
The test processes were stopped after validation.

The backend remains **experimental and opt-in** because render and collision
geometry differ. In the measured LiDAR frame, **834 of 49,984 beam positions
(1.67%)** had hit/miss disagreements. Among matched hits, median depth difference
was 0.33 mm, 95th percentile 11.17 cm, and maximum 73.20 m. This is not a
geometry-equivalent replacement for every training or validation workload.

## What changes

* Cameras defer their first capture until a stream or GBuffer has a consumer.
  Unassigned replicas no longer allocate persistent Lumen/view resources through
  an eager startup capture. The launcher waits for map loading and performs a
  paced renderer warmup before printing `READY`.

* Cameras already render with Unreal's GPU renderer. The local `ImageUtil.cpp`
  change removes the per-image blocking timestamp query and RHI-thread flush.
  One shared queue submits pending RHI commands without waiting for the RHI
  thread or GPU, polls GPU readback fences, then dispatches ready image
  conversion to background workers. Source textures explicitly transition to
  copy-source access and back to shader access. Mapping and unmapping remain on the render
  thread; actor access and delivery return to the game thread with a weak actor
  check and the original capture header. The camera staging pool is reused.
  Waiting images no longer occupy sleeping task-graph workers. GPU-to-host
  copies and client serialization still exist.
* `GpuSensorDispatcher.cpp` and `CarlaSensorTrace.usf` implement batched Vulkan
  inline ray queries against Unreal's acceleration structure. One compute thread
  traces each ray. LiDAR, semantic LiDAR, and radar use this backend when explicitly
  enabled. There is no silent CPU fallback when GPU mode is selected.
* Conservative per-frame sensor spheres limit ray-tracing scene residency to
  geometry reachable by the submitted rays. This uses distance-only culling,
  preserves surfaces behind the camera, and keeps an empty query scene until
  rays arrive. `r.RayTracing.ExternalQueryBounds=1` requires hardware ray-traced
  lighting to be disabled; High-quality software Lumen still renders on the GPU.
* Strict query residency prepares all required static geometry groups and builds
  pending BLAS before visibility and TLAS gathering. It evicts only unreferenced
  geometry and reports an explicit error if the configured 2048 MiB geometry
  budget cannot hold the query. Asynchronous cooked BLAS streaming is currently
  unsupported and also causes an explicit error. This deployment uses editor
  sources. Texture streaming uses a separate 2000 MiB pool.
* The engine's `r.RayTracing.ExternalQueries` switch keeps the ray-tracing scene
  available even when ray-traced lighting effects are disabled. The post-opaque
  renderer callback exposes its public `FSceneView` to the sensor pass.
* Replicated frames carry the primary episode timestamp using the existing
  recorder frame packet format. Secondary workers apply it before sensor capture,
  including the ROS clock. All workers must use this modified build. This fixes
  timestamps drifting with each worker's independent map-start time.
* Sensor output keeps the frame, timestamp, pose, ray order, channel association,
  and radar target velocity captured for the original tick. Existing LiDAR
  intensity/dropoff/noise processing and CARLA serialization remain on the CPU.
* Replicated ray sensors skip tracing when their stream has no consumer. This
  includes CARLA's forced stream activity, ROS subscriptions and disk recording.
* A headless primary handles physics. Four independent workers each render and
  trace on a selected GPU, using CARLA's existing round-robin stream routing.
  GPU workers use a hidden 160x90 viewport with lighting, shadows and
  postprocessing disabled so rays submitted after camera captures can query the
  current frame. Camera sensors keep their own rendering flags and resolution. They drain pending GPU rays before applying the
  next replicated world state, including frames queued during subscription setup.

## Current limits

The successful four-worker run used roughly 9.3–9.9 GiB per card, including
other resident display allocations where applicable. The 11 GiB cards have
limited headroom. Longer moving runs, larger sensor ranges, more cameras or
different maps need a fresh memory and correctness check. GPU query bounds use
render geometry within the full sensor reach; insufficient geometry residency
is an explicit failure, not permission to silently omit required surfaces.

GPU queries use **render geometry**, which can differ from Chaos collision
geometry. The GPU filter honors the collision response for CARLA's trace channel,
and ignores the sensor actor, but this does not make the two geometry sets equal.
Invisible collision-only objects, Nanite fallback triangles, foliage, masked
materials and animated meshes need scene-specific comparison. GPU queries treat
triangles as opaque, including masked material cutouts. The controlled cube test
checks basic equivalence; it does not certify every map or material.

Use synchronous stepping and consume every sensor's result before requesting the
next tick. This first implementation permits one outstanding batch per sensor to
preserve frame ordering and random-number sequencing. It reports an error instead
of silently dropping or relabeling frames if this constraint is violated. Do not
enable GPU tracing with `-nullrhi` or `no_rendering_mode=True`; a rendered view is
required to build the acceleration structure. Offscreen rendering is supported.

The four GPUs have separate memory and separate scenes. This does not combine
their VRAM or split one sensor's ray batch among four devices. Multiple sensor
streams are assigned across the workers. Physics, game logic, acceleration
structure submission, ray generation, postprocessing and network output still
consume CPU time. Replicating four worlds also has a CPU cost, which must be
included in performance comparisons.

## Launch commands

```bash
source /mnt/simulations/carla/carlab/host-setup/env.sh

# Inspect the exact primary and GPU worker commands without launching anything.
carla-multigpu --dry-run

# Run a primary plus four GPU workers. Ctrl-C stops the group it created.
carla-multigpu --gpus 0,1,2,3 --port 2000 --backend gpu

# Same worker layout using the original CPU ray tracing for comparison.
carla-multigpu --gpus 0,1,2,3 --port 2000 --backend cpu

# Wait for READY before connecting. In another terminal, run four cameras,
# four LiDARs and four radars.
# Supply the primary/worker PIDs from the launch log for CPU accounting.
carla-python /mnt/simulations/carla/carlab/host-setup/scripts/benchmark-sensors.py \
  --port 2000 --pids PRIMARY_PID,WORKER_PID_0,WORKER_PID_1,WORKER_PID_2,WORKER_PID_3 \
  --output /mnt/simulations/verification/sensors-four-gpu.json
```

For a single rendered server, use:

```bash
carla-sim -RenderOffScreen -graphicsadapter=3 -quality-level=High -ResX=640 -ResY=360 \
  -ExecCmds="r.Streaming.PoolSize 2000,r.RayTracing.UseReferenceBasedResidency 1,r.RayTracing.NumAlwaysResidentLODs 0,r.RayTracing.ResidentGeometryMemoryPoolSizeInMB 2048,carla.Sensors.GpuRayTracing 1,r.RayTracing.ExternalQueries 1,r.RayTracing.ExternalQueryBounds 1,r.RayTracing.ForceAllRayTracingEffects 0,r.RayTracing.Culling 0,r.RayTracing.Geometry.InstancedStaticMeshes.Culling 0"
```

`carla.Sensors.GpuSelfTest` is an Unreal console command. It spawns two controlled
cube meshes away from the map, with a nonblocking mesh in front of a blocking one,
then compares 65,536 GPU rays against Chaos. It checks hit/miss, distance, normal,
actor mapping and collision filtering, removes its actors, and writes
`/mnt/simulations/verification/gpu-ray-self-test.json` only on success. Its CPU
serial time and GPU end-to-end latency describe this controlled test, not general
simulation speedup.

The benchmark checks matching frame numbers, nonblack RGB output, finite sensor
values and valid ranges. It records measured CPU seconds per simulated second,
wall-clock throughput, sampled GPU utilization and VRAM. Compare CPU and GPU modes
with identical sensors, map, warmup and worker count before comparing one versus
four workers. No automatic success claim should be made merely because the source
compiles or a GPU appears in `nvidia-smi`.
