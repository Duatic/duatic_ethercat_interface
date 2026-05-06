#pragma once

#include "duatic_ethercat_interface/types.hpp"
#include "duatic_ethercat_interface/device_context.hpp"

namespace duatic::ethercat_interface
{
class EthercatDeviceBase
{
public:
  virtual DeviceId get_device_id() const
  {
    return context_->get_device_id();
  }

  EthercatDeviceBase(DeviceContext& context) : context_{ &context }
  {
  }
    /**
   * @brief on_pre_startup - before the bus pdo mapping / dc will be configured 
   */
  virtual void on_startup() {};
  /**
   * @brief on_pre_activate - before all devices are put into operational state but after bus pdo mapping has been
   * configured
   */
  virtual void on_pre_activate() {};
  /**
   * @brief on_pre_activate - after all devices have been put into operational
   */
  virtual void on_post_activate() {};
  /**
   * @brief on_pre_shutdown - before bus will be shutdown
   */
  virtual void on_pre_shutdown() {};
  /**
   * @brief on_post_shutdown - after bus will be shutdown
   */
  virtual void on_post_shutdown() {};

protected:


  DeviceContext* context_{ nullptr };
};
}  // namespace duatic::ethercat_interface
