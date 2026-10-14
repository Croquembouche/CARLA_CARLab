# Accumulated CARLA source and asset snapshot — 2026-10-14

Synchronizes the current local CARLA source changes and custom content with the publishing repository: sensor dispatch/readback and physical LiDAR work, traffic and vehicle behavior, camera diagnostics, town appearance/content overrides, vehicle interior/exterior source assets, and host launch/setup scripts.

Large changed assets are stored with Git LFS. Use `git lfs pull` after checkout. Apply the existing content/configuration override workflow from `carlab/SETUP.md`; this snapshot does not overwrite the live simulator installation.

The associated Control Center snapshot passed 352 Python tests, 19 MCP tests and 17 JavaScript test files. This publication did not rebuild the complete CARLA or Unreal engine and does not claim new performance acceptance. Diagnostic scripts, native build records and the continuing all-town 5 Hz goal checkpoints are retained in the private UE5_CARLab_Source repository under `carlab/verification`.

Runtime recordings, caches, compiled dependencies and temporary backups were not included in this update. Active source checkouts and the running scene remain in place.
