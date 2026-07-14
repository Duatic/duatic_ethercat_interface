/*
 * Copyright 2026 Duatic AG
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <span>
#include <cstdint>
#include <functional>
#include <ostream>
#include <iomanip>
#include <string>
#include <chrono>
namespace duatic::ethercat_interface
{

using TimeStamp = std::chrono::system_clock::time_point;
using HighPrecisionClock = std::chrono::high_resolution_clock;
using HighPrecisionTimeStamp = std::chrono::high_resolution_clock::time_point;

using DeviceId = uint16_t;
using SDOIndex = uint16_t;
using SDOSubIndex = uint8_t;
using RegisterAddress = uint16_t;

constexpr std::size_t maximum_visible_string_size = 255;

// Types for SDO (ServiceDataObject) interactions
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

// Types for FileOverEthercat interactions
struct FoEWriteResult
{
  bool success{ false };
  int working_counter{ 0 };

  operator bool() const
  {
    return success;
  }
};
struct FoEReadResult
{
  bool success{ false };
  int working_counter{ 0 };
  std::size_t actual_read_size{ 0 };
  std::span<const uint8_t> data{};

  operator bool() const
  {
    return success;
  }
};

// Types for Ethercat Register interactions
struct RegisterWriteResult
{
  bool success{ false };
  int working_counter{ 0 };

  operator bool() const
  {
    return success;
  }
};
struct RegisterReadResult
{
  bool success{ false };
  int working_counter{ 0 };
  std::size_t actual_read_size{ 0 };
  std::span<const uint8_t> data{};

  operator bool() const
  {
    return success;
  }
};

using SDOReadCallback = std::function<void(std::span<const uint8_t>, const DeviceId, const SDOIndex, const SDOSubIndex,
                                           const SDOReadResult&)>;
using SDOWriteCallback = std::function<void(const DeviceId, const SDOIndex, const SDOSubIndex, const SDOWriteResult&)>;

enum class SDOObjectCode : uint8_t
{
  Var = 0x07,
  Array = 0x08,
  Record = 0x09
};

enum class DataType : uint16_t
{
  UNKNOWN = 0x0000,

  BOOLEAN = 0x0001,
  INTEGER8 = 0x0002,
  INTEGER16 = 0x0003,
  INTEGER32 = 0x0004,
  UNSIGNED8 = 0x0005,
  UNSIGNED16 = 0x0006,
  UNSIGNED32 = 0x0007,
  REAL32 = 0x0008,
  VISIBLE_STRING = 0x0009,
  OCTET_STRING = 0x000A,
  UNICODE_STRING = 0x000B,
  TIME_OF_DAY = 0x000C,
  TIME_DIFFERENCE = 0x000D,

  DOMAIN = 0x000F,

  INTEGER24 = 0x0010,
  REAL64 = 0x0011,
  INTEGER40 = 0x0012,
  INTEGER48 = 0x0013,
  INTEGER56 = 0x0014,
  INTEGER64 = 0x0015,

  UNSIGNED24 = 0x0016,

  UNSIGNED40 = 0x0018,
  UNSIGNED48 = 0x0019,
  UNSIGNED56 = 0x001A,
  UNSIGNED64 = 0x001B,

  BIT1 = 0x0030,
  BIT2 = 0x0031,
  BIT3 = 0x0032,
  BIT4 = 0x0033,
  BIT5 = 0x0034,
  BIT6 = 0x0035,
  BIT7 = 0x0036,
  BIT8 = 0x0037,
};

struct DeviceInfo
{
  DeviceId id;
  std::string name;
  uint32_t vendor_id;
  uint32_t product_id;
  uint32_t revision;
  bool has_dc;
};

inline std::ostream& operator<<(std::ostream& os, const DeviceInfo& device)
{
  auto flags = os.flags();

  os << "DeviceInfo{\n"
     << "  id: " << static_cast<int>(device.id) << ",\n"
     << "  name: \"" << device.name << "\",\n"
     << "  vendor_id: 0x" << std::hex << std::uppercase << device.vendor_id << ",\n"
     << "  product_id: 0x" << std::hex << std::uppercase << device.product_id << ",\n"
     << "  revision: 0x" << std::hex << std::uppercase << device.revision << ",\n"
     << "  has_dc: " << std::boolalpha << device.has_dc << "\n"
     << "}";

  os.flags(flags);

  return os;
}

inline std::ostream& operator<<(std::ostream& os, SDOObjectCode code)
{
  switch (code) {
    case SDOObjectCode::Var:
      return os << "Var";

    case SDOObjectCode::Array:
      return os << "Array";

    case SDOObjectCode::Record:
      return os << "Record";

    default:
      return os << "Unknown(" << static_cast<int>(code) << ")";
  }
}

inline std::ostream& operator<<(std::ostream& os, DataType type)
{
  switch (type) {
    case DataType::UNKNOWN:
      return os << "UNKNOWN";

    case DataType::BOOLEAN:
      return os << "BOOLEAN";
    case DataType::INTEGER8:
      return os << "INTEGER8";
    case DataType::INTEGER16:
      return os << "INTEGER16";
    case DataType::INTEGER32:
      return os << "INTEGER32";

    case DataType::UNSIGNED8:
      return os << "UNSIGNED8";
    case DataType::UNSIGNED16:
      return os << "UNSIGNED16";
    case DataType::UNSIGNED32:
      return os << "UNSIGNED32";

    case DataType::REAL32:
      return os << "REAL32";

    case DataType::VISIBLE_STRING:
      return os << "VISIBLE_STRING";
    case DataType::OCTET_STRING:
      return os << "OCTET_STRING";
    case DataType::UNICODE_STRING:
      return os << "UNICODE_STRING";

    case DataType::TIME_OF_DAY:
      return os << "TIME_OF_DAY";
    case DataType::TIME_DIFFERENCE:
      return os << "TIME_DIFFERENCE";

    case DataType::DOMAIN:
      return os << "DOMAIN";

    case DataType::INTEGER24:
      return os << "INTEGER24";
    case DataType::REAL64:
      return os << "REAL64";

    case DataType::INTEGER40:
      return os << "INTEGER40";
    case DataType::INTEGER48:
      return os << "INTEGER48";
    case DataType::INTEGER56:
      return os << "INTEGER56";
    case DataType::INTEGER64:
      return os << "INTEGER64";

    case DataType::UNSIGNED24:
      return os << "UNSIGNED24";

    case DataType::UNSIGNED40:
      return os << "UNSIGNED40";
    case DataType::UNSIGNED48:
      return os << "UNSIGNED48";
    case DataType::UNSIGNED56:
      return os << "UNSIGNED56";
    case DataType::UNSIGNED64:
      return os << "UNSIGNED64";

    case DataType::BIT1:
      return os << "BIT1";
    case DataType::BIT2:
      return os << "BIT2";
    case DataType::BIT3:
      return os << "BIT3";
    case DataType::BIT4:
      return os << "BIT4";
    case DataType::BIT5:
      return os << "BIT5";
    case DataType::BIT6:
      return os << "BIT6";
    case DataType::BIT7:
      return os << "BIT7";
    case DataType::BIT8:
      return os << "BIT8";

    default:
      return os << "UNKNOWN_DATATYPE(0x" << std::hex << static_cast<uint16_t>(type) << ")";
  }
}

/**
 * @brief representation of ethercat device statesoot
 */
enum class EthercatDeviceState
{
  None,
  Init,
  PreOp,
  Boot,
  SafeOp,
  Operational,
};

inline std::ostream& operator<<(std::ostream& os, EthercatDeviceState state)
{
  switch (state) {
    case EthercatDeviceState::None:
      return os << "None";
    case EthercatDeviceState::Init:
      return os << "Init";
    case EthercatDeviceState::PreOp:
      return os << "PreOp";
    case EthercatDeviceState::Boot:
      return os << "Boot";
    case EthercatDeviceState::SafeOp:
      return os << "SafeOp";
    case EthercatDeviceState::Operational:
      return os << "Operational";
    default:
      return os << "Unknown";
  }
}

}  // namespace duatic::ethercat_interface
