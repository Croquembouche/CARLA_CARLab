// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/multigpu/primaryCommands.h"

// #include "carla/Logging.h"
#include "carla/multigpu/commands.h"
#include "carla/multigpu/primary.h"
#include "carla/multigpu/router.h"
#include "carla/streaming/detail/tcp/Message.h"
#include "carla/streaming/detail/Token.h"
#include "carla/streaming/detail/Types.h"

namespace carla {
namespace multigpu {

PrimaryCommands::PrimaryCommands() {
}

PrimaryCommands::PrimaryCommands(std::shared_ptr<Router> router) :
  _router(router) {
}

void PrimaryCommands::set_router(std::shared_ptr<Router> router) {
  _router = router;
}

// broadcast to all secondary servers the frame data
void PrimaryCommands::SendFrameData(carla::Buffer buffer) {
  _router->Write(MultiGPUCommand::SEND_FRAME, std::move(buffer));
  // log_info("sending frame command");
}

// broadcast to all secondary servers the map to load
void PrimaryCommands::SendLoadMap(std::string map) {
  carla::Buffer buf(reinterpret_cast<const unsigned char *>(map.c_str()), static_cast<size_t>(map.size() + 1));
  _router->Write(MultiGPUCommand::LOAD_MAP, std::move(buf));
}

// send to who the router wants the request for a token
token_type PrimaryCommands::SendGetToken(stream_id sensor_id, std::weak_ptr<Primary> server, uint32_t actor_id) {
  log_info("asking for a token");
  const uint32_t identifier = actor_id ? actor_id : sensor_id;
  carla::Buffer buf(reinterpret_cast<const carla::Buffer::value_type *>(&identifier), sizeof(identifier));
  auto fut = _router->WriteToOne(server, actor_id ? MultiGPUCommand::GET_TOKEN_BY_ACTOR : MultiGPUCommand::GET_TOKEN, std::move(buf));

  if (fut.wait_for(std::chrono::seconds(30)) != std::future_status::ready)
    throw std::runtime_error("GPU worker sensor subscription timed out");
  auto response = fut.get();
  if (response.buffer.size() != sizeof(carla::streaming::detail::token_data))
    throw std::runtime_error("GPU worker has not replicated the requested sensor actor");
  token_type new_token(*reinterpret_cast<carla::streaming::detail::token_data *>(response.buffer.data()));
  log_info("got a token: ", new_token.get_stream_id(), ", ", new_token.get_port());
  return new_token;
}

// send to know if a connection is alive
void PrimaryCommands::SendIsAlive() {
  std::string msg("Are you alive?");
  carla::Buffer buf(reinterpret_cast<const unsigned char *>(msg.c_str()), static_cast<size_t>(msg.size()));
  log_info("sending is alive command");
  auto fut = _router->WriteToNext(MultiGPUCommand::YOU_ALIVE, std::move(buf));
  auto response = fut.get();
  log_info("response from alive command: ", response.buffer.data());
}

void PrimaryCommands::SendEnableForROS(stream_id sensor_id) {
  // search if the sensor has been activated in any secondary server
  auto it = _servers.find(sensor_id);
  if (it != _servers.end()) {
    auto local_id = _tokens.at(sensor_id).get_stream_id();
    carla::Buffer buf(reinterpret_cast<carla::Buffer::value_type *>(&local_id),
                      static_cast<size_t>(sizeof(stream_id)));
    auto fut = _router->WriteToOne(it->second, MultiGPUCommand::ENABLE_ROS, std::move(buf));

    fut.get();
  } else {
    log_error("enable_for_ros for sensor", sensor_id, " not found on any server");
  }
}

void PrimaryCommands::SendDisableForROS(stream_id sensor_id) {
  // search if the sensor has been activated in any secondary server
  auto it = _servers.find(sensor_id);
  if (it != _servers.end()) {
    auto local_id = _tokens.at(sensor_id).get_stream_id();
    carla::Buffer buf(reinterpret_cast<carla::Buffer::value_type *>(&local_id),
                      static_cast<size_t>(sizeof(stream_id)));
    auto fut = _router->WriteToOne(it->second, MultiGPUCommand::DISABLE_ROS, std::move(buf));

    fut.get();
  } else {
    log_error("disable_for_ros for sensor", sensor_id, " not found on any server");
  }
}

bool PrimaryCommands::SendIsEnabledForROS(stream_id sensor_id) {
  // search if the sensor has been activated in any secondary server
  auto it = _servers.find(sensor_id);
  if (it != _servers.end()) {
    auto local_id = _tokens.at(sensor_id).get_stream_id();
    carla::Buffer buf(reinterpret_cast<carla::Buffer::value_type *>(&local_id),
                      static_cast<size_t>(sizeof(stream_id)));
    auto fut = _router->WriteToOne(it->second, MultiGPUCommand::IS_ENABLED_ROS, std::move(buf));

    auto response = fut.get();
    bool res = (*reinterpret_cast<bool *>(response.buffer.data()));
    return res;
  } else {
    log_error("is_enabled_for_ros for sensor", sensor_id, " not found on any server");
    return false;
  }
}

token_type PrimaryCommands::GetToken(stream_id sensor_id, double cost, uint32_t actor_id) {
  // search if the sensor has been activated in any secondary server
  auto it = _tokens.find(sensor_id);
  if (it != _tokens.end() && !_servers[sensor_id].expired()) {
    // return already activated sensor token
    log_debug("Using token from already activated sensor: ", it->second.get_stream_id(), ", ", it->second.get_port());
    return it->second;
  }
  else {
    // enable the sensor on one secondary server
    auto server = _router->ReserveSensor(sensor_id, cost);
    token_type token;
    try { token = SendGetToken(sensor_id, server, actor_id); }
    catch (...) { _router->ReleaseSensor(sensor_id); throw; }
    log_info("GPU_SENSOR_ROUTE stream", sensor_id, "port", token.get_port(), "cost", cost);
    // add to the maps
    _tokens[sensor_id] = token;
    _servers[sensor_id] = server;
    log_debug("Using token from new activated sensor: ", token.get_stream_id(), ", ", token.get_port());
    return token;
  }
}

void PrimaryCommands::ReleaseSensor(stream_id sensor_id) {
  _tokens.erase(sensor_id);
  _servers.erase(sensor_id);
  _router->ReleaseSensor(sensor_id);
}

void PrimaryCommands::EnableForROS(stream_id sensor_id) {
  auto it = _servers.find(sensor_id);
  if (it != _servers.end()) {
    SendEnableForROS(sensor_id);
  } else {
    // we need to activate the sensor in any server yet, and repeat
    GetToken(sensor_id);
    EnableForROS(sensor_id);
  }
}

void PrimaryCommands::DisableForROS(stream_id sensor_id) {
  auto it = _servers.find(sensor_id);
  if (it != _servers.end()) {
    SendDisableForROS(sensor_id);
  }
}

bool PrimaryCommands::IsEnabledForROS(stream_id sensor_id) {
  auto it = _servers.find(sensor_id);
  if (it != _servers.end()) {
    return SendIsEnabledForROS(sensor_id);
  }
  return false;
}

} // namespace multigpu
} // namespace carla
