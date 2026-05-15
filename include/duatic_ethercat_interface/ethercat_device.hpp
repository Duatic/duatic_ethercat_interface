#pragma once

#include <cstring>
#include <cassert>
#include <mutex>

#include "duatic_ethercat_interface/types.hpp"
#include "duatic_ethercat_interface/exceptions.hpp"
#include "duatic_ethercat_interface/object_dictionary.hpp"

namespace duatic::ethercat_interface
{
class EthercatBus;
class EthercatDeviceBase
{
public:
  using RawRxPdo = std::span<const uint8_t>;  // Const because the backend does not need to write to what is sent
  using RawTxPdo = std::span<uint8_t>;        // non const because the backend needs to write to that data range

  // Wrapper around the access to the actual pdo data
  // Provides the necessary mutex to safety access the corresponding data
  struct RxPdoWrapper
  {
    RawRxPdo raw;
    mutable std::mutex access_lock;
  };
  struct TxPdoWrapper
  {
    RawTxPdo raw;
    mutable std::mutex access_lock;
  };
  EthercatDeviceBase(EthercatBus* bus, const DeviceInfo& device_info);
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
  DeviceId get_device_id() const
  {
    return device_info_.id;
  }
  /**
   * @brief get_device_name - get the name of this specific device on the bus
   * @note this is the name obtained from the device
   */
  const std::string& get_device_name() const
  {
    return device_info_.name;
  }
  /**
   * @brief get_device_info - collection of all information that the backend usually can provide about a device
   */
  const DeviceInfo& get_device_info() const
  {
    return device_info_;
  }

  /**
   * @brief on_pdo_configured - callback which gets called as soon as the pdos has been setup and configured inn the
   * backend
   * @note This is already implemented in the EthercatDevice and GenericEthercatDevice classes. There is no need to use
   * override this yourself
   */
  virtual void on_pdo_configured(std::size_t configured_rx_pdo_size, std::size_t configured_tx_pdo_size) = 0;

  // Functions which provide a safe wrapper around the raw rx/tx data
  // These are usually only used by the backend
  virtual RxPdoWrapper& access_rx_pdo() = 0;
  virtual TxPdoWrapper& access_tx_pdo() = 0;

  // Helper functions for performing sdo read and writes
  template <typename T>
  std::optional<T> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0);
  template <typename T>
  std::optional<std::string> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0);

  template <typename T>
  bool sdo_write(const T value, const SDOIndex index, const SDOSubIndex sub_index = 0);
  bool sdo_write(const std::string value, const SDOIndex index, const SDOSubIndex sub_index = 0);

  const ObjectDictionary& read_od(bool full_read = false) const;

protected:
  // Internal pointer to the actual bus
  EthercatBus* bus_{ nullptr };
  DeviceInfo device_info_{};
};

/**
 * @brief Actual base class which should be used in the end by a user. It encodes the selected PDOs
 */
template <typename RXPDO, typename TXPDO>
class EthercatDevice final : public EthercatDeviceBase
{
public:
  struct PDOState
  {
    RXPDO rx;
    TXPDO tx;
  };

  EthercatDevice(EthercatBus* bus, DeviceInfo& device_info) : EthercatDeviceBase(bus, device_info)
  {
    rx_pdo_access_wrapper_.raw =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&pdo_state_.rx, sizeof(TXPDO)));
    tx_pdo_access_wrapper_.raw = std::span<uint8_t>(reinterpret_cast<uint8_t*>(&pdo_state_.tx, sizeof(RXPDO)));
  }

  RxPdoWrapper& access_rx_pdo() override final
  {
    return rx_pdo_access_wrapper_;
  }
  TxPdoWrapper& access_tx_pdo() override final
  {
    return tx_pdo_access_wrapper_;
  }
  void on_pdo_configured(std::size_t configured_rx_pdo_size, std::size_t configured_tx_pdo_size) override final
  {
    // Only check if sizes are matching
    if (configured_rx_pdo_size != sizeof(RXPDO)) {
      throw DeviceConfigurationError("RXPDO size does not match");
    }
    if (configured_tx_pdo_size != sizeof(TXPDO)) {
      throw DeviceConfigurationError("TXPDO size does not match");
    }
  }

  // By providing internal get/set functions for the PDO types
  // we avoid that any user needs to handle any locking
  // By returning a copy we make sure that a user does not need to worry about disturbing the internal state

  RXPDO get_rx_pdo() const
  {
    std::lock_guard<std::mutex> lock(rx_pdo_access_wrapper_.access_lock);
    return pdo_state_.rx;
  }
  void set_rx_pdo(const RXPDO& rx)
  {
    std::lock_guard<std::mutex> lock(rx_pdo_access_wrapper_.access_lock);
    pdo_state_.rx = rx;
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

/**
 * @brief a generic wrapper around an ethercat device which allows non typed access to the device
 * @note this is for sdk / tooling usage only
 */
class GenericEthercatDevice final : public EthercatDeviceBase
{
public:
  using RXPDO = std::vector<uint8_t>;
  using TXPDO = std::vector<uint8_t>;

  GenericEthercatDevice(EthercatBus* bus, const DeviceInfo& device_info) : EthercatDeviceBase(bus, device_info)
  {
  }
  void on_pdo_configured(std::size_t configured_rx_pdo_size, std::size_t configured_tx_pdo_size) override final
  {
    // For the generic device we need to allocate the necessary buffers
    rx_pdo_buffer_.resize(configured_rx_pdo_size, 0);
    tx_pdo_buffer_.resize(configured_tx_pdo_size, 0);

    rx_pdo_access_wrapper_.raw = std::span(rx_pdo_buffer_);
    tx_pdo_access_wrapper_.raw = std::span(tx_pdo_buffer_);

    pdo_initialized = true;
  }

  RXPDO get_rx_pdo() const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }

    std::lock_guard<std::mutex> lock(rx_pdo_access_wrapper_.access_lock);
    return rx_pdo_buffer_;
  }
  void set_rx_pdo(const RXPDO& rx)
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }
    std::lock_guard<std::mutex> lock(rx_pdo_access_wrapper_.access_lock);
    rx_pdo_buffer_ = rx;
  }
  TXPDO get_tx_pdo() const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access tx pdo - PDOs have not been configured yet");
    }
    std::lock_guard<std::mutex> lock(tx_pdo_access_wrapper_.access_lock);
    return tx_pdo_buffer_;
  }

private:
  bool pdo_initialized = false;
  std::vector<uint8_t> rx_pdo_buffer_;
  std::vector<uint8_t> tx_pdo_buffer_;

  RxPdoWrapper rx_pdo_access_wrapper_;
  TxPdoWrapper tx_pdo_access_wrapper_;
};

}  // namespace duatic::ethercat_interface
