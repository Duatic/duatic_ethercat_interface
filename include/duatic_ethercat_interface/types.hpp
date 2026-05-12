#pragma once
#include <cstdint>
#include <span>
#include <functional>
#include <ostream>
#include <iomanip>

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

enum class SDOObjectCode {
  Var,
  Array,
  Record
};

enum class DataType : uint16_t
{
    UNKNOWN         = 0x0000,

    BOOLEAN         = 0x0001,
    INTEGER8        = 0x0002,
    INTEGER16       = 0x0003,
    INTEGER32       = 0x0004,
    UNSIGNED8       = 0x0005,
    UNSIGNED16      = 0x0006,
    UNSIGNED32      = 0x0007,
    REAL32          = 0x0008,
    VISIBLE_STRING  = 0x0009,
    OCTET_STRING    = 0x000A,
    UNICODE_STRING  = 0x000B,
    TIME_OF_DAY     = 0x000C,
    TIME_DIFFERENCE = 0x000D,

    DOMAIN          = 0x000F,

    INTEGER24       = 0x0010,
    REAL64          = 0x0011,
    INTEGER40       = 0x0012,
    INTEGER48       = 0x0013,
    INTEGER56       = 0x0014,
    INTEGER64       = 0x0015,

    UNSIGNED24      = 0x0016,

    UNSIGNED40      = 0x0018,
    UNSIGNED48      = 0x0019,
    UNSIGNED56      = 0x001A,
    UNSIGNED64      = 0x001B,

    BIT1            = 0x0030,
    BIT2            = 0x0031,
    BIT3            = 0x0032,
    BIT4            = 0x0033,
    BIT5            = 0x0034,
    BIT6            = 0x0035,
    BIT7            = 0x0036,
    BIT8            = 0x0037,
};

struct DeviceInfo {
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
       << "  vendor_id: 0x"
       << std::hex << std::uppercase << device.vendor_id << ",\n"
       << "  product_id: 0x"
       << std::hex << std::uppercase << device.product_id << ",\n"
       << "  revision: 0x"
       << std::hex << std::uppercase << device.revision << ",\n"
       << "  has_dc: " << std::boolalpha << device.has_dc << "\n"
       << "}";

    os.flags(flags);

    return os;
}

}  // namespace duatic::ethercat_interface
