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

}  // namespace duatic::ethercat_interface
