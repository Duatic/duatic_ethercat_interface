#pragma once

namespace duatic::ethercat_interface::internal
{
/**
 * @brief INTERNAL interface representation for a backend implementation
 * @note this is an internal header and shall not be installed
 *
 * This interface class describes the abstract base interface for a specific backend implementation
 */
class BackendImplInterface
{
public:
  virtual int initialize() = 0;
  virtual int get_device_count() const;
  virtual ~BackendImplInterface() = default;
};
}  // namespace duatic::ethercat_interface::internal
