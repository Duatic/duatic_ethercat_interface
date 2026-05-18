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
  EthercatDeviceBase();
  virtual ~EthercatDeviceBase() = default;
  void configure(EthercatBus* bus, DeviceInfo device_info);

  /**
   * @brief on_configure - called when the device has been configured on a specific bus
   */
  virtual void on_configure()
  {
  }
  /**
   * @brief on_pre_startup - before the bus pdo mapping / dc will be configured
   */
  virtual void on_startup()
  {
  }
  /**
   * @brief on_pre_activate - before all devices are put into operational state but after bus pdo mapping has been
   * configured
   */
  virtual void on_pre_activate()
  {
  }
  /**
   * @brief on_pre_activate - after all devices have been put into operational
   */
  virtual void on_post_activate()
  {
  }
  /**
   * @brief on_pre_shutdown - before bus will be shutdown
   */
  virtual void on_pre_shutdown()
  {
  }
  /**
   * @brief on_post_shutdown - after bus will be shutdown
   */
  virtual void on_post_shutdown()
  {
  }
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

  virtual void update_write() = 0;
  virtual void update_read() = 0;

  // Helper functions for performing sdo read and writes
  template <typename T>
  std::optional<T> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0);
  template <typename T>
  std::optional<std::string> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0);

  template <typename T>
  bool sdo_write(const T value, const SDOIndex index, const SDOSubIndex sub_index = 0);
  bool sdo_write(const std::string value, const SDOIndex index, const SDOSubIndex sub_index = 0);

  ObjectDictionary read_od(bool full_read = false) const;

protected:
  // Internal pointer to the actual bus
  EthercatBus* bus_{ nullptr };
  DeviceInfo device_info_{};
};

/**
 * @brief a generic wrapper around an ethercat device which allows non typed access to the device
 * @note this is for sdk / tooling usage only
 */
class GenericEthercatDevice : public EthercatDeviceBase
{
public:
  using GenericRXPDO = std::vector<uint8_t>;
  using GenericTXPDO = std::vector<uint8_t>;

  GenericEthercatDevice()
  {
  }
  void on_pdo_configured(std::size_t configured_rx_pdo_size, std::size_t configured_tx_pdo_size) override
  {
    // For the generic device we need to allocate the necessary buffers
    rx_pdo_.resize(configured_rx_pdo_size, 0);
    tx_pdo_.resize(configured_tx_pdo_size, 0);

    pdo_initialized = true;
  }

  std::span<const uint8_t> get_generic_rx_pdo() const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }

    return rx_pdo_;
  }
  void set_generic_rx_pdo(std::span<const uint8_t> rx)
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }

    rx_pdo_.assign(rx.begin(), rx.end());
  }
  std::span<const uint8_t> get_generic_tx_pdo() const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access tx pdo - PDOs have not been configured yet");
    }

    return tx_pdo_;
  }

  void update_write() override;
  void update_read() override;

private:
  bool pdo_initialized = false;

  GenericRXPDO rx_pdo_;
  GenericTXPDO tx_pdo_;
};

/**
 * @brief Actual base class which should be used in the end by a user. It encodes the selected PDOs
 */
template <typename RXPDO, typename TXPDO>
class EthercatDevice final : public GenericEthercatDevice
{
public:
  static_assert(std::is_trivially_copyable_v<RXPDO>);
  static_assert(std::is_trivially_copyable_v<TXPDO>);

  EthercatDevice()
  {
  }

  void on_pdo_configured(std::size_t configured_rx_pdo_size, std::size_t configured_tx_pdo_size) override final
  {
    GenericEthercatDevice::on_pdo_configured(configured_rx_pdo_size, configured_tx_pdo_size);
    // Only check if sizes are matching
    if (configured_rx_pdo_size != sizeof(RXPDO)) {
      throw DeviceConfigurationError("RXPDO size does not match");
    }
    if (configured_tx_pdo_size != sizeof(TXPDO)) {
      throw DeviceConfigurationError("TXPDO size does not match");
    }
  }

  RXPDO get_rx_pdo() const
  {
    // TODO get rid of copy
    const auto generic = get_generic_rx_pdo();
    RXPDO temp;

    std::memcpy(&temp, generic.data(), sizeof(RXPDO));
    return temp;
  }
  void set_rx_pdo(const RXPDO& rx)
  {
    // TODO get rid of copy
    std::vector<uint8_t> temp(sizeof(RXPDO));
    std::memcpy(temp.data(), &rx, sizeof(RXPDO));

    set_generic_rx_pdo(temp);
  }
  TXPDO get_tx_pdo() const
  {
    // TODO get rid of copy
    const auto generic = get_generic_tx_pdo();
    TXPDO temp;
    std::memcpy(&temp, generic.data(), sizeof(TXPDO));
    return temp;
  }

private:
};

}  // namespace duatic::ethercat_interface
