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

#include <cstring>
#include <cassert>
#include <mutex>
#include <vector>
#include <string>

#include "duatic_ethercat_interface/types.hpp"
#include "duatic_ethercat_interface/exceptions.hpp"
#include "duatic_ethercat_interface/object_dictionary.hpp"
#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface
{

class EthercatDeviceBase
{
public:
  // List of callback functions a user can register
  struct Hooks
  {
    using FunctionPtr = std::function<void(void)>;
    FunctionPtr on_configure;
    FunctionPtr on_startup;
    FunctionPtr on_pdo_configured;
    FunctionPtr on_pre_activate;
    FunctionPtr on_post_activate;
    FunctionPtr on_pre_shutdown;
    FunctionPtr on_post_shutdown;
  };
  /**
   * @brief Helper enum to track if the device has already been configured by the bus
   * and if not handle it accordingly
   */
  enum class InstanceState
  {
    Created,
    BusConfigured,
    PdoConfigured
  };

  explicit EthercatDeviceBase(const Hooks& hooks = {});
  virtual ~EthercatDeviceBase() = default;
  /**
   * @brief configure - called by the bus to configure this device instace
   */
  void configure(EthercatBus* bus, DeviceInfo device_info);

  /**
   * @brief on_configure - called when the device has been configured on a specific bus
   */
  void on_configure()
  {
    if (hooks_.on_configure) {
      hooks_.on_configure();
    }
  }
  /**
   * @brief on_pre_startup - before the bus pdo mapping / dc will be configured
   */
  void on_startup()
  {
    if (hooks_.on_startup) {
      hooks_.on_startup();
    }
  }
  /**
   * @brief on_pre_activate - before all devices are put into operational state but after bus pdo mapping has been
   * configured
   */
  void on_pre_activate()
  {
    if (hooks_.on_pre_activate) {
      hooks_.on_pre_activate();
    }
  }
  /**
   * @brief on_pre_activate - after all devices have been put into operational
   */
  void on_post_activate()
  {
    if (hooks_.on_post_activate) {
      hooks_.on_post_activate();
    }
  }
  /**
   * @brief on_pre_shutdown - before bus will be shutdown
   */
  void on_pre_shutdown()
  {
    if (hooks_.on_pre_shutdown) {
      hooks_.on_pre_shutdown();
    }
  }
  /**
   * @brief on_post_shutdown - after bus will be shutdown
   */
  void on_post_shutdown()
  {
    if (hooks_.on_post_shutdown) {
      hooks_.on_post_shutdown();
    }
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
   */
  virtual void on_pdo_configured([[maybe_unused]] std::size_t configured_rx_pdo_size,
                                 [[maybe_unused]] std::size_t configured_tx_pdo_size)
  {
    if (hooks_.on_pdo_configured) {
      hooks_.on_pdo_configured();
    }
  }
  /**
   * @brief update_write - callback which gets called __before__ pdo data is sent over the line
   * @note do not manually call this function
   */
  virtual void update_write(const HighPrecisionTimeStamp& tp) = 0;
  /**
   * @brief update_read - callback which gets called __after__ pdo data has been read from the line
   * @note do not manually call this function
   */
  virtual void update_read(const HighPrecisionTimeStamp& tp) = 0;

  // Helper functions for performing sdo read and writes
  template <typename T>
  std::optional<T> sdo_read(const SDOIndex index, const SDOSubIndex sub_index = 0)
  {
    return bus_->sdo_read<T>(get_device_id(), index, sub_index);
  }

  template <typename T>
  bool sdo_write(const T value, const SDOIndex index, const SDOSubIndex sub_index = 0)
  {
    return bus_->sdo_write<T>(get_device_id(), value, index, sub_index);
  }

  /**
   * @brief read the full od from the device
   */
  ObjectDictionary read_od(bool full_read = false) const;

  /**
   * @brief echange_device_state - try to enforce a state transition of the specified device into the specified state
   * @param blocking - if true wait for a certain amount of time (determined by the backend) for the transistion to
   * finish
   * @return true in case of success
   * @note not thread safe
   */
  bool change_device_state(const EthercatDeviceState target_state, bool blocking = true);
  /**
   * @brief foe_write - perform a file write via ethercat
   * @note not thread safe
   */
  FoEWriteResult foe_write(const std::string& file_name, std::span<const uint8_t> data);
  /**
   * @brief foe_read - perform a file read via ethercat
   * @note not thread safe
   */
  FoEReadResult foe_read(const std::string& file_name, std::span<uint8_t> buffer);

protected:
  // Internal pointer to the actual bus
  EthercatBus* bus_{ nullptr };

private:
  DeviceInfo device_info_{};
  Hooks hooks_{};
};

/**
 * @brief a generic wrapper around an ethercat device which allows non typed access to the device
 * @note this is for sdk / tooling usage only
 */
class GenericEthercatDevice final : public EthercatDeviceBase
{
public:
  using GenericRXPDO = std::vector<uint8_t>;
  using GenericTXPDO = std::vector<uint8_t>;

  explicit GenericEthercatDevice(const Hooks& hooks = {}) : EthercatDeviceBase(hooks)
  {
  }
  void on_pdo_configured(std::size_t configured_rx_pdo_size, std::size_t configured_tx_pdo_size) override
  {
    // For the generic device we need to allocate the necessary buffers
    rx_pdo_.resize(configured_rx_pdo_size, 0);
    rx_pdo_last_write_time_ = HighPrecisionClock::now();
    tx_pdo_.resize(configured_tx_pdo_size, 0);
    tx_pdo_last_read_time_ = HighPrecisionClock::now();

    pdo_initialized = true;

    EthercatDeviceBase::on_pdo_configured(configured_rx_pdo_size, configured_tx_pdo_size);
  }

  /**
   * @brief Access the currently configured RX PDO (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus.
   * This access the raw data which needs to be interpreted afterwards
   */
  std::span<const uint8_t> get_generic_rx_pdo() const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }
    std::lock_guard<std::mutex> lock(pdo_update_mutex_);
    return rx_pdo_;
  }
  /**
   * @brief Configure the next RX PDO to be sent to the device (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied to the bus on the next
   * update_write call
   */
  void set_generic_rx_pdo(std::span<const uint8_t> rx)
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access rx pdo - PDOs have not been configured yet");
    }

    std::lock_guard<std::mutex> lock(pdo_update_mutex_);
    rx_pdo_.assign(rx.begin(), rx.end());
  }
  /**
   * @brief Access the latest received TX PDO (tx == data the device sends the the maste receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied from the bus at the
   * update_read call
   */
  std::span<const uint8_t> get_generic_tx_pdo() const
  {
    if (!pdo_initialized) {
      throw DeviceConfigurationError("Cannot access tx pdo - PDOs have not been configured yet");
    }

    std::lock_guard<std::mutex> lock(pdo_update_mutex_);
    return tx_pdo_;
  }

  /**
   * @brief Access the currently configured RX PDO (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus.
   */
  template <typename RXPDO>
  RXPDO get_rx_pdo() const
  {
    static_assert(std::is_trivially_copyable_v<RXPDO>);
    if (sizeof(RXPDO) != rx_pdo_.size()) {
      throw DeviceConfigurationError("RXPDO size (" + std::to_string(sizeof(RXPDO)) +
                                     ") does not match configured size (" + std::to_string(rx_pdo_.size()) + ")");
    }
    // TODO(firesurfer) get rid of copy
    const auto generic = get_generic_rx_pdo();
    RXPDO temp;

    std::memcpy(&temp, generic.data(), sizeof(RXPDO));
    return temp;
  }
  /**
   * @brief Configure the next RX PDO to be sent to the device (rx == data the master sends and the device receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied to the bus on the next
   * update_write call
   */
  template <typename RXPDO>
  void set_rx_pdo(const RXPDO& rx)
  {
    static_assert(std::is_trivially_copyable_v<RXPDO>);
    if (sizeof(RXPDO) != rx_pdo_.size()) {
      throw DeviceConfigurationError("RXPDO size (" + std::to_string(sizeof(RXPDO)) +
                                     ") does not match configured size (" + std::to_string(rx_pdo_.size()) + ")");
    }
    // TODO(firesurfer) get rid of copy
    std::vector<uint8_t> temp(sizeof(RXPDO));
    std::memcpy(temp.data(), &rx, sizeof(RXPDO));

    set_generic_rx_pdo(temp);
  }
  /**
   * @brief Access the latest received TX PDO (tx == data the device sends the the maste receives)
   * @note this function is fully thread safe and does not block the bus. The data is copied from the bus at the
   * update_read call
   */
  template <typename TXPDO>
  TXPDO get_tx_pdo() const
  {
    static_assert(std::is_trivially_copyable_v<TXPDO>);
    if (sizeof(TXPDO) != tx_pdo_.size()) {
      throw DeviceConfigurationError("TXPDO size (" + std::to_string(sizeof(TXPDO)) +
                                     ") does not match configured size (" + std::to_string(tx_pdo_.size()) + ")");
    }
    // TODO(firesurfer) get rid of copy
    const auto generic = get_generic_tx_pdo();
    TXPDO temp;
    std::memcpy(&temp, generic.data(), sizeof(TXPDO));
    return temp;
  }
  /**
   * @brief Latest RX PDO write time stamp
   * Obtain the latest point in time when the internal rx pdo copy was written on the bus
   * @note this function is thread safe (within API usage scheme)
   */
  const HighPrecisionTimeStamp& get_last_rx_pdo_write_time() const
  {
    return rx_pdo_last_write_time_;
  }
  /**
   * @brief Latest TX PDO read time stamp
   * Obtain the latest point in the time when the internal tx pdo copy was updated from the bus
   * @note this function is thread safe (within API usage scheme)
   */
  const HighPrecisionTimeStamp& get_last_tx_pdo_read_time() const
  {
    return tx_pdo_last_read_time_;
  }

  // Actual implementation of the write/read functions
  // These functions copy the data to the ethercat backend
  // DO NOT call them by hand
  void update_write(const HighPrecisionTimeStamp& tp) final;
  void update_read(const HighPrecisionTimeStamp& tp) final;

private:
  bool pdo_initialized = false;

  // Access should only be done via the get/set_generic_pdo methods
  GenericRXPDO rx_pdo_;
  HighPrecisionTimeStamp rx_pdo_last_write_time_;
  GenericTXPDO tx_pdo_;
  HighPrecisionTimeStamp tx_pdo_last_read_time_;
  mutable std::mutex pdo_update_mutex_;
};

}  // namespace duatic::ethercat_interface
