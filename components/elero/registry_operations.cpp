#include "device_registry.h"

namespace esphome::elero {

OperationResult DeviceRegistry::sync_configuration() {
  if (!nvs_enabled_ || !prefs_initialized_)
    return {OperationStatus::REJECTED, "Persistent configuration unavailable"};
  if (global_preferences == nullptr || !global_preferences->sync())
    return {OperationStatus::FAILED, "Flash write failed; changes remain pending"};
  return {OperationStatus::APPLIED, "Saved to flash"};
}

OperationResult DeviceRegistry::save_device(const NvsDeviceConfig &config) {
  if (!nvs_enabled_ || !prefs_initialized_)
    return {OperationStatus::REJECTED, "Persistent configuration unavailable"};
  if (!config.is_remote() && (!config.src_address || !config.channel))
    return {OperationStatus::REJECTED, "Remote address and channel required"};
  std::string error;
  if (upsert(config, &error) == nullptr)
    return {OperationStatus::REJECTED, error};
  return sync_configuration();
}

OperationResult DeviceRegistry::delete_device(uint32_t address, DeviceType type) {
  if (!nvs_enabled_ || !prefs_initialized_)
    return {OperationStatus::REJECTED, "Persistent configuration unavailable"};
  if (find(address, type) == nullptr)
    return {OperationStatus::REJECTED, "Device no longer exists"};
  if (!remove(address, type))
    return {OperationStatus::FAILED, "Device deletion failed; group changes may have applied"};
  return sync_configuration();
}

OperationResult DeviceRegistry::save_group(const NvsGroupConfig &config) {
  if (!nvs_enabled_ || !prefs_initialized_)
    return {OperationStatus::REJECTED, "Persistent configuration unavailable"};
  std::string error;
  if (upsert_group(config, &error) == nullptr)
    return {OperationStatus::REJECTED, error};
  return sync_configuration();
}

OperationResult DeviceRegistry::delete_group(const char *id) {
  if (!nvs_enabled_ || !prefs_initialized_)
    return {OperationStatus::REJECTED, "Persistent configuration unavailable"};
  if (find_group(id) == nullptr)
    return {OperationStatus::REJECTED, "Group no longer exists"};
  if (!remove_group(id))
    return {OperationStatus::FAILED, "Group deletion failed"};
  return sync_configuration();
}

OperationResult DeviceRegistry::save_hub_name(const std::string &name) {
  if (!nvs_enabled_ || !prefs_initialized_)
    return {OperationStatus::REJECTED, "Persistent configuration unavailable"};
  std::string error;
  set_hub_name_override(name, &error);
  if (!error.empty())
    return {OperationStatus::FAILED, error};
  // Also retry a previous failed sync when the requested value is unchanged.
  return sync_configuration();
}

OperationResult DeviceRegistry::command_device(Device &dev, uint8_t command) {
  if (!dev.active || dev.is_remote())
    return {OperationStatus::REJECTED, "Device is not controllable"};
  if (command == packet::command::INVALID)
    return {OperationStatus::REJECTED, "Invalid command"};
  bool queued;
  if (command == packet::command::CHECK)
    queued = request_check(dev);
  else if (dev.is_cover())
    queued = command == packet::command::TILT ? command_cover_tilt(dev) : command_cover(dev, command);
  else
    queued = command_light(dev, command);
  return queued ? OperationResult{OperationStatus::QUEUED, "Command queued"}
                : OperationResult{OperationStatus::REJECTED, "Command queue full"};
}

OperationResult DeviceRegistry::send_group_command(const char *id, uint8_t command) {
  std::string error;
  if (!command_saved_group(id, command, &error))
    return {OperationStatus::REJECTED, error};
  return {OperationStatus::QUEUED, "Group command queued"};
}

void DeviceRegistry::notify_channel_command(const ChannelCommandResult &result) {
  for (auto *adapter : adapters_)
    adapter->on_channel_command_result(result);
}

}  // namespace esphome::elero
