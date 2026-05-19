#pragma once

#include <algorithm>
#include <ostream>
#include <span>

#include "duatic_ethercat_interface/types.hpp"

namespace duatic::ethercat_interface::internal::soem
{

enum class BusState
{
  PreInit,
  Initialized,
  Configured,
  Activated,
  Operational,
  Shutdown
};

enum class SDOTransferDirection
{
  Read,
  Write
};

inline std::ostream& operator<<(std::ostream& os, BusState state)
{
  switch (state) {
    case BusState::PreInit:
      return os << "PreInit";
    case BusState::Initialized:
      return os << "Initialized";
    case BusState::Configured:
      return os << "Configured";
    case BusState::Activated:
      return os << "Activated";
    case BusState::Operational:
      return os << "Operational";
    case BusState::Shutdown:
      return os << "Shutdown";
  }

  return os << "Unknown";
}

inline bool can_attach_device(const BusState state)
{
  return state == BusState::Initialized;
}

inline bool has_device_on_bus(const DeviceId device_id, const int device_count)
{
  return device_id > 0 && device_id <= device_count;
}

inline bool has_managed_device(std::span<const DeviceId> device_ids, const DeviceId device_id)
{
  return std::find(device_ids.begin(), device_ids.end(), device_id) != device_ids.end();
}

inline SDOTransferDirection queued_sdo_read_direction()
{
  return SDOTransferDirection::Read;
}

inline SDOTransferDirection queued_sdo_write_direction()
{
  return SDOTransferDirection::Write;
}

inline bool queued_sdo_read_success(const int working_counter, const int actual_size, const int requested_size)
{
  return working_counter > 0 && actual_size == requested_size;
}

inline bool queued_sdo_write_success(const int working_counter)
{
  return working_counter > 0;
}

inline bool should_shutdown_via_preop(const BusState state)
{
  return state == BusState::Configured || state == BusState::Activated || state == BusState::Operational;
}

}  // namespace duatic::ethercat_interface::internal::soem
