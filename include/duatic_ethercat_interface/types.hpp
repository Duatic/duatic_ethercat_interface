#pragma once
#include <cstdint>
#include <span>
#include <functional>

namespace duatic::ethercat_interface
{
using DeviceId = uint16_t;
using SDOIndex = uint16_t;
using SDOSubIndex = uint8_t;

using SDOReadCallback =
    std::function<void(std::span<const uint8_t>, const DeviceId, const SDOIndex, const SDOSubIndex)>;
using SDOWriteCallback = std::function<void(const bool, const DeviceId, const SDOIndex, const SDOSubIndex)>;

constexpr std::size_t maximum_visible_string_size = 255;

struct SDOReadResult
{
  bool success{ false };
  int actual_size_read{ 0 };
  int working_counter{ 0 };

  operator bool() const
  {
    return success;
  }
};

struct SDOWriteResult
{
  bool success{ false };
  int working_counter{ 0 };

  operator bool() const
  {
    return success;
  }
};

}  // namespace duatic::ethercat_interface
