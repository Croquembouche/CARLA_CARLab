# Network simulation interface

Install [CARLA_MCP](https://github.com/Croquembouche/CARLA_MCP) at `/mnt/simulations/control-center` using its [setup guide](https://github.com/Croquembouche/CARLA_MCP/blob/main/docs/SETUP.md). Open `http://SERVER_IP:8095/` using the address of the machine on which you installed it.

- [Usage, recording, ROS 2 and planner documentation](https://github.com/Croquembouche/CARLA_MCP/blob/main/docs/OPERATIONS.md)
- [MCP connection and capability guide](https://github.com/Croquembouche/CARLA_MCP/blob/main/docs/mcp.md)
- [Application source](https://github.com/Croquembouche/CARLA_MCP)
- [Published verification reports](https://github.com/Croquembouche/CARLA_MCP/tree/main/data)

The optional user service is `carla-control-center.service`. It starts the web interface; GPU simulator workers start when requested through that interface. New recordings are stored in `/mnt/simulations/control-center/data/recordings`.
