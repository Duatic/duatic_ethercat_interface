#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface
{
EthercatDeviceBase::EthercatDeviceBase()
{
}
void EthercatDeviceBase::configure(EthercatBus* bus, DeviceInfo device_info)
{
  device_info_ = device_info;
  bus_ = bus;
  on_configure();
}
template <typename T>
std::optional<T> EthercatDeviceBase::sdo_read(const SDOIndex index, const SDOSubIndex sub_index)
{
  static_assert(!std::is_same_v<T, std::string>);
  // This is the actual data instance we use
  T data{};
  // And this is just a safe representation (pointer + size) to it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(&data), sizeof(T));

  if (!bus_->read_sdo_untyped(buffer, get_device_id(), index, sub_index)) {
    return std::nullopt;
  }

  return data;
}

template <typename T>
std::optional<std::string> EthercatDeviceBase::sdo_read(const SDOIndex index, const SDOSubIndex sub_index)
{
  // Create a buffer of the maximum possible string length
  std::vector<char> data(maximum_visible_string_size, 0);
  // Create a uin8t_t span reprensentation of it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(data.data()), data.size());
  const auto result = bus_->read_sdo_untyped(buffer, get_device_id(), index, sub_index);
  if (!result) {
    // Something during the read failed
    return std::nullopt;
  }

  // Create an std::string out of it
  return std::string(data.begin(), data.begin() + result.actual_size_read);
}

template std::optional<uint8_t> EthercatDeviceBase::sdo_read<uint8_t>(SDOIndex, SDOSubIndex);
template std::optional<int8_t> EthercatDeviceBase::sdo_read<int8_t>(SDOIndex, SDOSubIndex);
template std::optional<uint16_t> EthercatDeviceBase::sdo_read<uint16_t>(SDOIndex, SDOSubIndex);
template std::optional<int16_t> EthercatDeviceBase::sdo_read<int16_t>(SDOIndex, SDOSubIndex);
template std::optional<uint32_t> EthercatDeviceBase::sdo_read<uint32_t>(SDOIndex, SDOSubIndex);
template std::optional<int32_t> EthercatDeviceBase::sdo_read<int32_t>(SDOIndex, SDOSubIndex);
template std::optional<uint64_t> EthercatDeviceBase::sdo_read<uint64_t>(SDOIndex, SDOSubIndex);
template std::optional<int64_t> EthercatDeviceBase::sdo_read<int64_t>(SDOIndex, SDOSubIndex);
template std::optional<float> EthercatDeviceBase::sdo_read<float>(SDOIndex, SDOSubIndex);
template std::optional<double> EthercatDeviceBase::sdo_read<double>(SDOIndex, SDOSubIndex);

template <typename T>
bool EthercatDeviceBase::sdo_write(const T value, const SDOIndex index, const SDOSubIndex sub_index)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
  const auto result = bus_->write_sdo_untyped(data, get_device_id(), index, sub_index);
  if (!result) {
    // Something during the write failed
    return false;
  }

  return true;
}

bool EthercatDeviceBase::sdo_write(const std::string value, const SDOIndex index, const SDOSubIndex sub_index)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  const auto result = bus_->write_sdo_untyped(data, get_device_id(), index, sub_index);
  if (!result) {
    // Something during the write failed
    return false;
  }

  return true;
}

template bool EthercatDeviceBase::sdo_write<uint8_t>(const uint8_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<int8_t>(const int8_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<uint16_t>(const uint16_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<int16_t>(const int16_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<uint32_t>(const uint32_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<int32_t>(const int32_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<uint64_t>(const uint64_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<int64_t>(const int64_t, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<float>(const float, SDOIndex, SDOSubIndex);
template bool EthercatDeviceBase::sdo_write<double>(const double, SDOIndex, SDOSubIndex);

ObjectDictionary EthercatDeviceBase::read_od(bool full_read) const
{
  return bus_->read_od(get_device_id(), full_read);
}

void GenericEthercatDevice::update_write()
{
  bus_->write_rx_pdo(get_device_id(), rx_pdo_);
}
void GenericEthercatDevice::update_read()
{
  tx_pdo_ = bus_->read_tx_pdo(get_device_id());
};

}  // namespace duatic::ethercat_interface
