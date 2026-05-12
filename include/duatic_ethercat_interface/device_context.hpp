#pragma once

#include <optional>
#include <string>

#include "duatic_ethercat_interface/types.hpp"
#include "duatic_ethercat_interface/object_dictionary.hpp"

namespace duatic::ethercat_interface
{

class EthercatBus;
class DeviceContext
{
public:
  DeviceContext(EthercatBus* bus, DeviceId device_id) : bus_(bus), device_id_(device_id)
  {
  }

  DeviceId get_device_id() const
  {
    return device_id_;
  }

  template <typename T>
  std::optional<T> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0);

  template <typename T>
  std::optional<std::string> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0);

  template <typename T>
  bool sdo_write(const T value, const SDOIndex index, const SDOSubIndex sub_index = 0);

  bool sdo_write(const std::string value, const SDOIndex index, const SDOSubIndex sub_index = 0);

  const ObjectDictionary& read_od(bool full_read = false) const;

private:
  EthercatBus* bus_{ nullptr };
  DeviceId device_id_{ 0 };
};
}  // namespace duatic::ethercat_interface
