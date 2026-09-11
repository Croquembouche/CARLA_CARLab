# Network simulation interface

Open [CARLA Control Center](http://128.175.213.232:8095) from this host or another
computer with network access. The second interface is [10.100.100.7:8095](http://10.100.100.7:8095).

- [Full usage, recording, ROS 2 and planner documentation](linux/control-center/README.md)
- [Design prepared before implementation](linux/control-center/design/DESIGN.md)
- [Source and application files](linux/control-center/)
- [Verification reports](linux/control-center/data/)

The separate service is `carla-control-center.service`. It starts the web
interface automatically; start the GPU simulator from the interface when needed.
Recordings remain on this Simulations drive under `linux/control-center/data/recordings`.
