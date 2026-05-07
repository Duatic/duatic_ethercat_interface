#include "duatic_ethercat_interface/device_context.hpp"

#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface
{

template <typename T>
std::optional<T> DeviceContext::sdo_read(const SDOIndex index, const SDOSubIndex sub_index)
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
std::optional<std::string> DeviceContext::sdo_read(const SDOIndex index, const SDOSubIndex sub_index)
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

template std::optional<uint8_t> DeviceContext::sdo_read<uint8_t>(SDOIndex, SDOSubIndex);
template std::optional<int8_t> DeviceContext::sdo_read<int8_t>(SDOIndex, SDOSubIndex);
template std::optional<uint16_t> DeviceContext::sdo_read<uint16_t>(SDOIndex, SDOSubIndex);
template std::optional<int16_t> DeviceContext::sdo_read<int16_t>(SDOIndex, SDOSubIndex);
template std::optional<uint32_t> DeviceContext::sdo_read<uint32_t>(SDOIndex, SDOSubIndex);
template std::optional<int32_t> DeviceContext::sdo_read<int32_t>(SDOIndex, SDOSubIndex);
template std::optional<uint64_t> DeviceContext::sdo_read<uint64_t>(SDOIndex, SDOSubIndex);
template std::optional<int64_t> DeviceContext::sdo_read<int64_t>(SDOIndex, SDOSubIndex);
template std::optional<float> DeviceContext::sdo_read<float>(SDOIndex, SDOSubIndex);
template std::optional<double> DeviceContext::sdo_read<double>(SDOIndex, SDOSubIndex);

}  // namespace duatic::ethercat_interface
