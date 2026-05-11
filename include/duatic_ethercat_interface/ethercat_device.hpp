#pragma once

#include <cstring>
#include <cassert>

#include "duatic_ethercat_interface/types.hpp"
#include "duatic_ethercat_interface/device_context.hpp"

namespace duatic::ethercat_interface
{

class EthercatDeviceBase
{
public:
  using RawRxPdo = std::span<uint8_t>;
  using RawTxPdo = std::span<const uint8_t>;

  struct RxPdoWrapper
  {
    RawRxPdo raw;
    std::mutex access_lock;
  };
  struct TxPdoWrapper
  {
    RawTxPdo raw;
    std::mutex access_lock;
  };
  EthercatDeviceBase(DeviceContext& context) : context_{ &context }
  {
  }
  virtual ~EthercatDeviceBase() = default;

  /**
   * @brief on_pre_startup - before the bus pdo mapping / dc will be configured
   */
  virtual void on_startup(){};
  /**
   * @brief on_pre_activate - before all devices are put into operational state but after bus pdo mapping has been
   * configured
   */
  virtual void on_pre_activate(){};
  /**
   * @brief on_pre_activate - after all devices have been put into operational
   */
  virtual void on_post_activate(){};
  /**
   * @brief on_pre_shutdown - before bus will be shutdown
   */
  virtual void on_pre_shutdown(){};
  /**
   * @brief on_post_shutdown - after bus will be shutdown
   */
  virtual void on_post_shutdown(){};
  /**
   * @brief get_device_id - get the id of this specific device on the bus
   */
  virtual DeviceId get_device_id() const
  {
    return context_->get_device_id();
  }

  // Functions which provide a safe wrapper around the raw rx/tx data
  virtual RxPdoWrapper& access_rx_pdo() = 0;
  virtual TxPdoWrapper& access_tx_pdo() = 0;

protected:
  DeviceContext* context_{ nullptr };
};

/**
 * @brief Actual base class which should be used in the end by a user. It encodes the selected PDOs
 */
template <typename RXPDO, typename TXPDO>
class EthercatDevice : public EthercatDeviceBase
{
public:
  struct PDOState
  {
    RXPDO rx;
    TXPDO tx;
  };

  EthercatDevice(DeviceContext& context) : EthercatDeviceBase(context)
  {
    rx_pdo_access_wrapper_.raw =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&pdo_state_.tx, sizeof(TXPDO)));
    tx_pdo_access_wrapper_.raw = std::span<uint8_t>(reinterpret_cast<uint8_t*>(&pdo_state_.rx, sizeof(RXPDO)));
  }

  RxPdoWrapper& access_rx_pdo() override final
  {
    return rx_pdo_access_wrapper_;
  }
  TxPdoWrapper& access_tx_pdo() override final
  {
    return tx_pdo_access_wrapper_;
  }

protected:
  // By providing internal get/set functions for the PDO types
  // we avoid that any downstream class needs to handle any locking
  // By returning a copy we make sure that a user does not need to worry about disturbing the internal state

  RXPDO get_rx_pdo() const
  {
    std::lock_guard<std::mutex> lock(rx_pdo_access_wrapper_.access_lock);
    return pdo_state_.rx;
  }
  void set_tx_pdo(const TXPDO& tx)
  {
    std::lock_guard<std::mutex> lock(tx_pdo_access_wrapper_.access_lock);
    pdo_state_.tx = tx;
  }
  TXPDO get_tx_pdo() const
  {
    std::lock_guard<std::mutex> lock(tx_pdo_access_wrapper_.access_lock);
    return pdo_state_.tx;
  }

private:
  PDOState pdo_state_;
  RxPdoWrapper rx_pdo_access_wrapper_;
  TxPdoWrapper tx_pdo_access_wrapper_;
};

}  // namespace duatic::ethercat_interface
