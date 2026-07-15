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

#include "duatic_ethercat_interface/ethercat_bus.hpp"

#include <mutex>     // NOLINT(build/include_order)
#include <thread>    // NOLINT(build/include_order)
#include <queue>     // NOLINT(build/include_order)
#include <atomic>    // NOLINT(build/include_order)
#include <optional>  // NOLINT(build/include_order)

#include "duatic_ethercat_interface/exceptions.hpp"
#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/distributed_clock_sync.hpp"

#include "duatic_ethercat_interface/object_dictionary.hpp"

#include "duatic_ethercat_interface/internal/backend_impl.hpp"
#include "duatic_ethercat_interface/internal/soem/soem_context.hpp"
#include "duatic_message_logger/log.hpp"
// Implementation of the EthercatBus for the SOEM library
namespace duatic::ethercat_interface
{

using namespace internal::soem;  // NOLINT(build/namespaces)

std::string to_string(const AlStatus& status)
{
  return std::string(ec_ALstatuscode2string(status));
}

enum class BusState
{
  PreInit,
  Initialized,
  Configured,
  Activated,
  Operational,
  Shutdown
};

struct SDOTransfer
{
  enum class Direction
  {
    Read,
    Write
  };
  DeviceId device_id;
  SDOIndex index;
  SDOSubIndex sub_index;
  Direction direction;
  void* data;
  int data_size;
  int timeout;
  std::function<void(bool, int, int)> transfer_finished_cb;
};

inline std::ostream& operator<<(std::ostream& os, BusState state)
{
  switch (state) {
    case BusState::PreInit:
      return os << "PreInit";
    case BusState::Initialized:
      return os << "Initialized";
    case BusState::Configured:
      return os << "Configured";
    case BusState::Activated:
      return os << "Activated";
    case BusState::Operational:
      return os << "Operational";
    case BusState::Shutdown:
      return os << "Shutdown";
  }

  return os << "Unknown";
}

static constexpr ec_state map_to_soem_device_state(const EthercatDeviceState state)
{
  switch (state) {
    case EthercatDeviceState::None:
      return ec_state::EC_STATE_NONE;
    case EthercatDeviceState::Init:
      return ec_state::EC_STATE_INIT;
    case EthercatDeviceState::PreOp:
      return ec_state::EC_STATE_PRE_OP;
    case EthercatDeviceState::Boot:
      return ec_state::EC_STATE_BOOT;
    case EthercatDeviceState::SafeOp:
      return ec_state::EC_STATE_SAFE_OP;
    case EthercatDeviceState::Operational:
      return ec_state::EC_STATE_OPERATIONAL;

    default:
      return ec_state::EC_STATE_NONE;
  }
}
static constexpr EthercatDeviceState map_from_soem_device_state(const ec_state state)
{
  switch (state) {
    case ec_state::EC_STATE_NONE:
      return EthercatDeviceState::None;
    case ec_state::EC_STATE_INIT:
      return EthercatDeviceState::Init;
    case ec_state::EC_STATE_PRE_OP:
      return EthercatDeviceState::PreOp;
    case ec_state::EC_STATE_BOOT:
      return EthercatDeviceState::Boot;
    case ec_state::EC_STATE_SAFE_OP:
      return EthercatDeviceState::SafeOp;
    case ec_state::EC_STATE_OPERATIONAL:
      return EthercatDeviceState::Operational;

    default:
      return EthercatDeviceState::None;
  }
}

struct EthercatBus::BackendImpl
{
  explicit BackendImpl(const Parameters& params)
    : params_(params)
    , logger_(logging::get_logger_with_default_sink("SOEM-Backend"))
    , dc_sync_(params.dc_cycle_time, params_.master_send_offset)
  {
  }
  ~BackendImpl()
  {
    shutdown();
  }

  int initialize()
  {
    // Initialize the context - this initializes the passed ethernet interface
    if (const auto ec = ecx_init(&context_.context, params_.interface.c_str()); ec <= 0) {
      throw BackendError("Failed to open interface: " + interface_ + " Run as root!", Backend::SOEM, ec);
    }

    // This discovers devices
    const int device_count = ecx_config_init(&context_.context, FALSE);
    if (device_count <= 0) {
      throw BackendError("No devices found on the bus", Backend::SOEM, device_count);
    }

    update_bus_state(BusState::Initialized);
    return device_count;
  }

  const Parameters& get_parameters() const
  {
    return params_;
  }

  SDOReadResult read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                 const SDOSubIndex sub_index = 0, bool check_size = true,
                                 const int timeout = EC_TIMEOUTRXM)
  {
    // NOTE we only report some errors as exceptions as for example working counter too low can happen also in normal
    // operation In this case simply false is returned

    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    const int requested_size = static_cast<int>(data.size());

    if (get_bus_state() == BusState::Operational) {
      std::atomic<SDOReadResult> result;
      std::binary_semaphore transfer_finished{ 0 };
      SDOTransfer transfer{ .device_id = device_id,
                            .index = index,
                            .sub_index = sub_index,
                            .direction = SDOTransfer::Direction::Read,
                            .data = data.data(),
                            .data_size = requested_size,
                            .timeout = timeout,
                            .transfer_finished_cb = [&](const bool success, const int actual_size, const int wkc) {
                              result = SDOReadResult{ .success = success,
                                                      .actual_size_read = actual_size,
                                                      .working_counter = wkc };
                              transfer_finished.release();
                            } };
      {
        // Lock it and enque the transfer
        std::lock_guard<std::mutex> lock(sdo_update_mutex_);
        sdo_transfer_queue_.push(transfer);
      }  // lock is now released

      // Wait until it is finished
      transfer_finished.acquire();

      return result.load();
    } else {
      // It theory it is possible to call the sdo functions even if the bus is not activated yet from multiple threads
      std::lock_guard<std::mutex> lock(sdo_update_mutex_);
      // Directly perform the read
      int actual_size = requested_size;
      const int wkc =
          ecx_SDOread(&context_.context, device_id, index, sub_index, FALSE, &actual_size, data.data(), timeout);
      if (wkc < 0) {
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                                << ") for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                                << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                                << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        return SDOReadResult{ .success = false, .actual_size_read = actual_size, .working_counter = wkc };
      }

      if (check_size && requested_size != actual_size) {
        logging::error(logger_) << "Device id  " << device_id << ": Size mismatch (expected " << requested_size
                                << " bytes, read " << actual_size << " bytes) for reading SDO (ID: 0x"
                                << std::setfill('0') << std::setw(4) << std::hex << index << ", SID 0x"
                                << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(sub_index)
                                << ")." << std::endl;
        throw BackendError("SDORead size mismatch", Backend::SOEM, actual_size);
      }
      return SDOReadResult{ .success = true, .actual_size_read = actual_size, .working_counter = wkc };
    }
  }
  void read_sdo_untyped_async(const SDOReadCallback& cb, std::span<uint8_t> data, const DeviceId device_id,
                              const SDOIndex index, const SDOSubIndex sub_index = 0, bool check_size = true,
                              const int timeout = EC_TIMEOUTRXM)
  {
    // NOTE we only report some errors as exceptions as for example working counter too low can happen also in normal
    // operation In this case simply false is returned

    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    const int requested_size = static_cast<int>(data.size());

    if (get_bus_state() == BusState::Operational) {
      SDOTransfer transfer{ .device_id = device_id,
                            .index = index,
                            .sub_index = sub_index,
                            .direction = SDOTransfer::Direction::Read,
                            .data = data.data(),
                            .data_size = requested_size,
                            .timeout = timeout,
                            .transfer_finished_cb = [=](const bool success, const int actual_size, const int wkc) {
                              SDOReadResult result{ .success = success,
                                                    .actual_size_read = actual_size,
                                                    .working_counter = wkc };
                              // Just call the callback
                              // NOTE this is in a different thread now
                              cb(data, device_id, index, sub_index, result);
                            } };
      {
        // Lock it and enque the transfer
        std::lock_guard<std::mutex> lock(sdo_update_mutex_);
        sdo_transfer_queue_.push(transfer);
      }  // lock is now released

    } else {
      // It theory it is possible to call the sdo functions even if the bus is not activated yet from multiple threads
      std::lock_guard<std::mutex> lock(sdo_update_mutex_);
      // Directly perform the read
      int actual_size = requested_size;
      const int wkc =
          ecx_SDOread(&context_.context, device_id, index, sub_index, FALSE, &actual_size, data.data(), timeout);
      SDOReadResult result{};
      if (wkc < 0) {
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                                << ") for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                                << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                                << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        result = SDOReadResult{ .success = false, .actual_size_read = actual_size, .working_counter = wkc };
      } else {
        result = SDOReadResult{ .success = true, .actual_size_read = actual_size, .working_counter = wkc };
      }

      if (check_size && requested_size != actual_size) {
        logging::error(logger_) << "Device id  " << device_id << ": Size mismatch (expected " << requested_size
                                << " bytes, read " << actual_size << " bytes) for reading SDO (ID: 0x"
                                << std::setfill('0') << std::setw(4) << std::hex << index << ", SID 0x"
                                << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(sub_index)
                                << ")." << std::endl;
        throw BackendError("SDORead size mismatch", Backend::SOEM, actual_size);
      }
      // In case of success just call the callback
      // NOTE this is in the same thread now
      cb(data, device_id, index, sub_index, result);
    }
  }

  SDOWriteResult write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    if (get_bus_state() == BusState::Operational) {
      std::atomic<SDOWriteResult> result;
      std::binary_semaphore transfer_finished{ 0 };
      SDOTransfer transfer{ .device_id = device_id,
                            .index = index,
                            .sub_index = sub_index,
                            .direction = SDOTransfer::Direction::Write,
                            .data = const_cast<uint8_t*>(data.data()),  // TODO introduce write/read transfers?
                            .data_size = static_cast<int>(data.size()),
                            .timeout = timeout,
                            .transfer_finished_cb = [&](const bool success, [[maybe_unused]] const int actual_size,
                                                        const int wkc) {
                              result = SDOWriteResult{ .success = success, .working_counter = wkc };
                              transfer_finished.release();
                            } };
      {
        // Lock it and enque the transfer
        std::lock_guard<std::mutex> lock(sdo_update_mutex_);
        sdo_transfer_queue_.push(transfer);
      }  // lock is now released

      // Wait until it is finished
      transfer_finished.acquire();

      return result.load();
    } else {
      // It theory it is possible to call the sdo functions even if the bus is not activated yet from multiple threads
      std::lock_guard<std::mutex> lock(sdo_update_mutex_);
      // Directly perform the write
      const int wkc = ecx_SDOwrite(&context_.context, device_id, index, sub_index, FALSE, static_cast<int>(data.size()),
                                   const_cast<uint8_t*>(data.data()), timeout);
      if (wkc < 0) {
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                                << ") for writing SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                                << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                                << static_cast<uint16_t>(sub_index) << ")." << std::endl;

        return SDOWriteResult{ .success = false, .working_counter = wkc };
      }
      return SDOWriteResult{ .success = true, .working_counter = wkc };
    }
  }

  void write_sdo_untyped_async(const SDOWriteCallback& cb, std::span<const uint8_t> data, const DeviceId device_id,
                               const SDOIndex index, const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    if (get_bus_state() == BusState::Operational) {
      SDOTransfer transfer{ .device_id = device_id,
                            .index = index,
                            .sub_index = sub_index,
                            .direction = SDOTransfer::Direction::Write,
                            .data = const_cast<uint8_t*>(data.data()),  // TODO introduce write/read transfers?
                            .data_size = static_cast<int>(data.size()),
                            .timeout = timeout,
                            .transfer_finished_cb = [=](const bool success, [[maybe_unused]] const int actual_size,
                                                        const int wkc) {
                              SDOWriteResult result{ .success = success, .working_counter = wkc };
                              cb(device_id, index, sub_index, result);
                            } };
      {
        // Lock it and enque the transfer
        std::lock_guard<std::mutex> lock(sdo_update_mutex_);
        sdo_transfer_queue_.push(transfer);
      }  // lock is now released

    } else {
      // It theory it is possible to call the sdo functions even if the bus is not activated yet from multiple threads
      std::lock_guard<std::mutex> lock(sdo_update_mutex_);
      // Directly perform the write
      const int wkc = ecx_SDOwrite(&context_.context, device_id, index, sub_index, FALSE, static_cast<int>(data.size()),
                                   const_cast<uint8_t*>(data.data()), timeout);
      SDOWriteResult result{};
      if (wkc < 0) {
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                                << ") for writing SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                                << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                                << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        result = SDOWriteResult{ .success = false, .working_counter = wkc };
      } else {
        result = SDOWriteResult{ .success = true, .working_counter = wkc };
      }
      cb(device_id, index, sub_index, result);
    }
  }

  int get_device_count() const
  {
    return context_.ecatSlavecount_;
  }

  void attach_device(const DeviceId device_id, std::shared_ptr<EthercatDeviceBase> device)
  {
    // As we need to perfrom the right PDO mapping we need to make sure that the bus is in the right state
    if (get_bus_state() != BusState::Initialized) {
      throw BackendError("Cannot attach device to bus - bus it not in the correct state", Backend::SOEM);
    }
    // Check if the device even is on the bus
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Device id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }

    // And that we not already handle a device with this id
    if (has_device(device_id)) {
      throw BackendError("Device with id: " + std::to_string(device_id) + " is already handled by this bus",
                         Backend::SOEM);
    }
    devices_.emplace_back(device);
  }

  void startup()
  {
    if (get_bus_state() != BusState::Initialized) {
      throw BackendError("Cannot perform startup - bus needs to be in 'Initialized' state", Backend::SOEM);
    }
    // Give the device the chance to prepare its pdo mapping
    for (auto& device : devices_) {
      device->on_startup();
    }

    // Disable symmetrical transfers.
    /// Needs to be done before the mapping
    if (params_.block_LRW) {
      context_.context.grouplist[0].blockLRW = 1;
    } else {
      context_.context.grouplist[0].blockLRW = 0;
    }
    // Perform PDO setup
    // As there is no way to properly determine the pdo map size before hand we let the user
    // externally configure the buffer size that we are using
    io_map_.resize(params_.pdo_buffer_size, 0);
    // And then do the actual configuration
    const int final_iomap_size = ecx_config_map_group(&context_.context, io_map_.data(), 0);
    logging::info(logger_) << "IOMap size: " << final_iomap_size << std::endl;

    if (final_iomap_size > params_.pdo_buffer_size) {
      throw BackendError("Calculated pdo size is: " + std::to_string(final_iomap_size) +
                         " whereas the configured buffer size is: " + std::to_string(params_.pdo_buffer_size));
    }

    // Setup distributed clock
    if (!ecx_configdc(&context_.context)) {
      if (params_.dc_enabled) {
        logger_.error("No devices with DC support found on the bus");
        throw BackendError("No devices wiht DC support found on the bus but DC sync is enabled");
      } else {
        logger_.info("No devices with DC support found on the bus");
      }
    }
    // Setup dc sync (NOTE we only support sync0 at the moment)
    if (params_.dc_enabled) {
      for (uint16_t i = 1; i <= static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
        if (context_.ecatSlavelist_[i].hasdc) {
          ecx_dcsync0(&context_.context, i, true, static_cast<uint32_t>(params_.dc_cycle_time.count()),
                      static_cast<int32_t>(params_.dc_sync0_shift.count()));
        }
      }
    }

    update_bus_state(BusState::Configured);

    // Notify all devices that the pdos have now been configured
    for (auto& device : devices_) {
      device->on_pdo_configured(context_.ecatSlavelist_[device->get_device_id()].Obytes,
                                context_.ecatSlavelist_[device->get_device_id()].Ibytes);
    }
  }

  void activate()
  {
    if (get_bus_state() != BusState::Configured) {
      throw BackendError("Cannot perform activate - bus needs to be in 'Configured' state", Backend::SOEM);
    }

    // Give the device the chance to perform any last steps before we bring them into safeop
    for (auto& device : devices_) {
      device->on_pre_activate();
    }

    // Now put all devices into safeop first (this is timewise non critical)

    // First we request it from every slave
    for (auto& device : devices_) {
      if (!set_device_target_state(device->get_device_id(), ec_state::EC_STATE_SAFE_OP)) {
        // Shutdown should handle a safe exit in all situations and bus states
        shutdown();
        throw BackendError("Could set target state for device - aborting activation", Backend::SOEM);
      }
    }
    // Now we wait for all slaves to reach it
    for (auto& device : devices_) {
      if (!wait_for_device_target_state(device->get_device_id(), ec_state::EC_STATE_SAFE_OP)) {
        shutdown();
        throw BackendError("Device did not reach 'SAFE_OP' state - aborting activation", Backend::SOEM);
      }
    }

    // Bus is now in activated state so the update method knows that it needs to push devices into OPERATIONAL first
    update_bus_state(BusState::Activated);

    // Give the devices the chance to perfrom any last steps before the operational stage starts
    for (auto& device : devices_) {
      device->on_post_activate();
    }
  }

  void shutdown()
  {
    // The bus has not been initialized - no need to shut it down
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Shutdown) {
      update_bus_state(BusState::Shutdown);
      return;
    }
    logging::info(logger_) << "Performing bus shutdown: " << params_.interface;
    // We only initialized the bus - just close the connection
    if (get_bus_state() == BusState::Initialized) {
      update_bus_state(BusState::Shutdown);
      ecx_close(&context_.context);
      return;
    }

    for (auto& device : devices_) {
      device->on_pre_shutdown();
    }

    if (get_bus_state() == BusState::Configured || get_bus_state() == BusState::Activated ||
        get_bus_state() == BusState::Operational) {
      // Bring all devices into pre-op
      // Note: using device id 0 targets all devices on the bus
      set_device_target_state(0, ec_state::EC_STATE_PRE_OP);
      wait_for_device_target_state(0, ec_state::EC_STATE_PRE_OP);

      ecx_close(&context_.context);
      update_bus_state(BusState::Shutdown);
      for (auto& device : devices_) {
        device->on_post_shutdown();
      }

      return;
    }

    throw BackendError("Invalid BusState in shutdown - dont know what to do", Backend::SOEM);
  }

  bool has_device(const DeviceId device_id) const
  {
    return std::find_if(devices_.begin(), devices_.end(),
                        [&](const auto& d) { return d->get_device_id() == device_id; }) != devices_.end();
  }
  /**
   * @brief check if the specific device has been found on the bus
   * @note this is different to "has_device" which checks if we handle a specific device
   */
  bool has_device_on_bus(const DeviceId id) const
  {
    return id > 0 && id <= get_device_count();
  }
  std::optional<std::chrono::nanoseconds> update()
  {
    // First thing after startup is to bring the devices into operational state
    if (get_bus_state() == BusState::Activated) {
      for (const auto& device : devices_) {
        set_device_target_state(device->get_device_id(), ec_state::EC_STATE_OPERATIONAL);
      }

      // Update the context once (needed for the device state check)
      ecx_readstate(&context_.context);

      bool all_in_operational = true;
      for (const auto& device : devices_) {
        if (get_device_state(device->get_device_id()) != ec_state::EC_STATE_OPERATIONAL) {
          all_in_operational = false;
        }
      }

      if (all_in_operational) {
        // TODO add timeout ?
        update_bus_state(BusState::Operational);
      }
    }
    // Perform the actual pdo read/write actions
    const auto dc_sync_correction_factor = internal_pdo_update();

    {
      std::lock_guard<std::mutex> lock(sdo_update_mutex_);
      // Check if the sdo transfer queue is not empty and handle 1 transfer
      if (!sdo_transfer_queue_.empty()) {
        auto transfer = sdo_transfer_queue_.front();
        sdo_transfer_queue_.pop();

        // SDO Timeouts:
        // Note that the standard SDO timeouts may block the RT update loop
        // Nevertheless it is better to perform them at this location than having some kind of mutex
        // lock in order to handle seperate access to the bus via SOEM
        // if this is critical the user can pass a smaller timeout from the outside
        if (transfer.direction == SDOTransfer::Direction::Read) {
          int actual_size = transfer.data_size;
          const int wkc = ecx_SDOread(&context_.context, transfer.device_id, transfer.index, transfer.sub_index, FALSE,
                                      &actual_size, transfer.data, transfer.timeout);
          transfer.transfer_finished_cb(wkc >= 0, actual_size, wkc);
        } else {
          const int wkc = ecx_SDOwrite(&context_.context, transfer.device_id, transfer.index, transfer.sub_index, FALSE,
                                       transfer.data_size, transfer.data, transfer.timeout);
          transfer.transfer_finished_cb(wkc >= 0, transfer.data_size, wkc);
        }
      }
    }  // lock is now released

    return dc_sync_correction_factor;
  }

  std::vector<SDOEntry> read_od_from_device(const DeviceId device_id, bool full_read)
  {
    std::vector<SDOEntry> result;

    ec_ODlistt od_list{};

    // 1. Read Object Dictionary list (top level objects)
    if (!ecx_readODlist(&context_.context, device_id, &od_list)) {
      throw DeviceNotFound("Failed to read OD list from device", Backend::SOEM);
    }

    // 2. Iterate over objects
    for (int i = 0; i < od_list.Entries; ++i) {
      SDOEntry entry{};
      ecx_readODdescription(&context_.context, static_cast<uint16_t>(i), &od_list);
      entry.index = static_cast<SDOIndex>(od_list.Index[i]);
      entry.obj_type = static_cast<SDOObjectCode>(od_list.ObjectCode[i]);
      entry.data_type = static_cast<DataType>(od_list.DataType[i]);
      entry.count_sub_indices = static_cast<std::size_t>(od_list.MaxSub[i]);
      entry.name = od_list.Name[i];

      // Always push entry even if we don't expand subindices
      result.push_back(entry);
    }

    // 3. Optional: full subindex scan
    if (full_read) {
      for (auto& obj : result) {
        // Var types do now have sub objects
        if (obj.obj_type == SDOObjectCode::Var) {
          continue;
        }
        // Clear previous subentries just in case
        obj.sub_entries.clear();
        ec_OElistt oe_list{};

        // Read subindex metadata for this object
        // Pass the item number (should match the index in od_list)
        int item_index = -1;
        for (int idx = 0; idx < od_list.Entries; ++idx) {
          if (od_list.Index[idx] == static_cast<uint16_t>(obj.index)) {
            item_index = idx;
            break;
          }
        }

        if (item_index < 0 || !ecx_readOE(&context_.context, static_cast<uint16_t>(item_index), &od_list, &oe_list)) {
          continue;  // some objects may not support OE info
        }

        // IMPORTANT: OElist.Entries = number of subentries returned
        for (int s = 0; s < oe_list.Entries; ++s) {
          SDOSubEntry sub{};
          sub.index = static_cast<SDOSubIndex>(s);
          sub.name = oe_list.Name[s];
          sub.data_type = static_cast<DataType>(oe_list.DataType[s]);
          sub.size = static_cast<std::size_t>(oe_list.BitLength[s] / 8);

          obj.sub_entries.push_back(sub);
        }
      }
    }

    return result;
  }

  std::vector<DeviceInfo> scan()
  {
    std::vector<DeviceInfo> result;
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Need to initialize bus first for a scan", Backend::SOEM);
    }

    for (int i = 1; i <= context_.ecatSlavecount_; i++) {
      result.emplace_back(DeviceInfo{ .id = static_cast<DeviceId>(i),
                                      .name = std::string(context_.ecatSlavelist_[i].name),
                                      .vendor_id = context_.ecatSlavelist_[i].eep_man,
                                      .product_id = context_.ecatSlavelist_[i].eep_id,
                                      .revision = context_.ecatSlavelist_[i].eep_rev,
                                      .has_dc = static_cast<bool>(context_.ecatSlavelist_[i].hasdc) });
    }
    return result;
  }
  DeviceInfo scan(const DeviceId device_id)
  {
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Need to initialize bus first for a scan", Backend::SOEM);
    }
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Cannot scan for device - it is not on the bus", Backend::SOEM);
    }
    return DeviceInfo{ .id = static_cast<DeviceId>(device_id),
                       .name = std::string(context_.ecatSlavelist_[device_id].name),
                       .vendor_id = context_.ecatSlavelist_[device_id].eep_man,
                       .product_id = context_.ecatSlavelist_[device_id].eep_id,
                       .revision = context_.ecatSlavelist_[device_id].eep_rev,
                       .has_dc = static_cast<bool>(context_.ecatSlavelist_[device_id].hasdc) };
  }

  std::vector<uint8_t> read_rx_pdo(const DeviceId device_id) const
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Initialized) {
      throw BackendError("Cannot access pdo for device - pdo needs to be configured first");
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Cannot access pdo for device - it is not on the bus", Backend::SOEM);
    }

    const auto size = context_.ecatSlavelist_[device_id].Obytes;

    std::vector<uint8_t> result(size, 0);

    std::lock_guard<std::mutex> lock(pdo_update_mutex_);
    std::memcpy(result.data(), context_.ecatSlavelist_[device_id].outputs, size);

    return result;
  }

  void write_rx_pdo(const DeviceId device_id, const std::vector<uint8_t>& data)
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Initialized) {
      throw BackendError("Cannot access pdo for device - pdo needs to be configured first");
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Cannot access pdo for device - it is not on the bus", Backend::SOEM);
    }

    const auto size = context_.ecatSlavelist_[device_id].Obytes;
    if (data.size() != size) {
      throw BackendError("PDO size mismatch - cannot assign", Backend::SOEM);
    }

    std::lock_guard<std::mutex> lock(pdo_update_mutex_);
    std::memcpy(context_.ecatSlavelist_[device_id].outputs, data.data(), size);
  }

  std::vector<uint8_t> read_tx_pdo(const DeviceId device_id) const
  {
    if (get_bus_state() == BusState::PreInit || get_bus_state() == BusState::Initialized) {
      throw BackendError("Cannot access pdo for device - pdo needs to be configured first");
    }
    if (!has_device_on_bus(device_id)) {
      throw BackendError("Cannot access pdo for device - it is not on the bus", Backend::SOEM);
    }

    const auto size = context_.ecatSlavelist_[device_id].Ibytes;
    std::vector<uint8_t> result(size, 0);

    std::lock_guard<std::mutex> lock(pdo_update_mutex_);
    std::memcpy(result.data(), context_.ecatSlavelist_[device_id].inputs, size);

    return result;
  }

  FoEWriteResult foe_write(const DeviceId device_id, const std::string& file_name, std::span<const uint8_t> data)
  {
    const int wkc =
        ecx_FOEwrite(&context_.context, device_id, const_cast<char*>(file_name.c_str()), 0,
                     static_cast<int>(data.size()), const_cast<uint8_t*>(data.data()), EC_TIMEOUTRXM * 1000);
    return FoEWriteResult{ .success = wkc == 1, .working_counter = wkc };
  }
  FoEReadResult foe_read(const DeviceId device_id, const std::string& file_name, std::span<uint8_t> buffer)
  {
    int actual_size = static_cast<int>(buffer.size());
    const int wkc = ecx_FOEread(&context_.context, device_id, const_cast<char*>(file_name.c_str()), 0, &actual_size,
                                buffer.data(), EC_TIMEOUTRXM * 1000);

    return FoEReadResult{ .success = wkc == 1,
                          .working_counter = wkc,
                          .actual_read_size = static_cast<std::size_t>(actual_size),
                          .data = std::span(buffer.data(), static_cast<std::size_t>(actual_size)) };
  }
  bool set_device_target_state(const DeviceId device_id, ec_state target_state)
  {
    context_.ecatSlavelist_[device_id].state = target_state;
    return ecx_writestate(&context_.context, device_id) > 0;
  }
  bool wait_for_device_target_state(const DeviceId device_id, ec_state target_state, int timeout = EC_TIMEOUTSTATE)
  {
    return ecx_statecheck(&context_.context, device_id, target_state, timeout) == target_state;
  }
  ec_state get_device_state(const DeviceId device_id)
  {
    // Note ecx_readstate needs to be called once before to update the state
    return static_cast<ec_state>(context_.ecatSlavelist_[device_id].state);
  }

  RegisterReadResult read_register_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                           const RegisterAddress address, bool check_size)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    if (data.size() > std::numeric_limits<uint16_t>::max()) {
      throw std::logic_error("Requested data size too large");
    }

    // TODO(firesurfer) With SOEM we can only do fully blocking calls. Nevertheless it makes sense to implement the
    // mechanism as with SDO read/writes to handle thread synchronisation

    const uint16_t size = static_cast<uint16_t>(data.size());
    const int wkc = ecx_FPRD(&context_.ecat_port, context_.ecatSlavelist_[device_id].configadr, address, size,
                             data.data(), EC_TIMEOUTRET3);

    return RegisterReadResult{
      .success = wkc > 0,
      .working_counter = wkc,
      .actual_read_size = size,
      .data = data,
    };
  }

  RegisterWriteResult write_register_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                             const RegisterAddress address)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (!has_device_on_bus(device_id)) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    if (data.size() > std::numeric_limits<uint16_t>::max()) {
      throw std::logic_error("Requested data size too large");
    }

    // TODO(firesurfer) With SOEM we can only do fully blocking calls. Nevertheless it makes sense to implement the
    // mechanism as with SDO
    // read/writes to handle thread synchronisation
    const uint16_t size = static_cast<uint16_t>(data.size());
    const int wkc = ecx_FPWR(&context_.ecat_port, context_.ecatSlavelist_[device_id].configadr, address, size,
                             const_cast<uint8_t*>(data.data()), EC_TIMEOUTRET3);
    return RegisterWriteResult{ .success = wkc > 0, .working_counter = wkc };
  }

  DiagnosticsSnapshot diagnostics(bool force_update)
  {
    // We allow in non operational bus state to directly obtain device and bus diagnostics
    // This is definitely not the prefered way but necessary for some applications
    if (force_update && get_bus_state() == BusState::Operational) {
      throw std::runtime_error("Bus diagnostics force update may not be used why the bus is in operational state !");
    } else if (force_update) {
      update_diagnostics(0, 0);
      return latest_diagnostics_;
    } else {
      std::lock_guard lock(diagnostics_mutex_);
      return latest_diagnostics_;
    }
  }

private:
  // Parameterization
  const std::string interface_;
  const Parameters params_;

  // Ethercat context and bus state tracking
  EthercatContext context_;
  // Memory where SOEM will store its pdos aftwards
  std::vector<uint8_t> io_map_;
  std::atomic<BusState> state_{ BusState::PreInit };

  // Device management
  std::vector<std::shared_ptr<EthercatDeviceBase>> devices_;

  // Everything update thread related
  std::mutex sdo_update_mutex_;
  mutable std::mutex pdo_update_mutex_;

  // Synchronization of sdo read/writes into update thread
  std::queue<SDOTransfer> sdo_transfer_queue_;

  logging::Logger logger_;
  DCSyncController dc_sync_;

  DiagnosticsSnapshot latest_diagnostics_;
  std::size_t current_selected_diagnostics_slave_ = 1;
  std::mutex diagnostics_mutex_;

  void update_bus_state(BusState state)
  {
    logging::info(logger_) << "Bus transitioning into state: " << state;
    state_ = state;
  }
  BusState get_bus_state() const
  {
    return state_;
  }

  std::optional<std::chrono::nanoseconds> internal_pdo_update()
  {
    std::optional<std::chrono::nanoseconds> update_rate_correction_factor{ std::nullopt };
    // Obtain the current timestamp we stamp on the read /write times
    const auto now = std::chrono::high_resolution_clock::now();
    // Take the latest tx pdo state from every device
    for (auto& device : devices_) {
      device->update_write(now);
    }

    // Stored outside the lock block so that the diagnostics can access it
    int wkc{ 0 };
    int expected_wkc{ 0 };

    {  // Important to lock after update_write(), otherwise update_write will deadlock
      std::lock_guard<std::mutex> lock(pdo_update_mutex_);
      if (ecx_send_processdata(&context_.context) <= 0) {
        logging::error(logger_) << "Failed to send process data" << std::endl;
      }
      wkc = ecx_receive_processdata(&context_.context, EC_TIMEOUTRET);

      expected_wkc = context_.context.grouplist[0].outputsWKC * 2 + context_.context.grouplist[0].inputsWKC;
      if (wkc < expected_wkc) {
        logging::warning(logger_) << params_.interface << " Working counter too low (pdo): " << wkc
                                  << " expected wkc: " << expected_wkc << std::endl;
      }
      // Run DC clock pi controller to calculate a correction factor which then can be used by executor to shift the
      // update rate
      if (params_.dc_enabled && context_.ecatSlavelist_[0].hasdc) {
        update_rate_correction_factor = dc_sync_.update(std::chrono::nanoseconds{ context_.ecatDcTime_ });
      }
    }  // Important to unlock here, otherwise update_read() will deadlock

    // Update the pdo state of every device
    for (auto& device : devices_) {
      device->update_read(now);
    }

    // Try to create a diagnostics snapshot
    update_diagnostics(wkc, expected_wkc);
    return update_rate_correction_factor;
  }

  void update_diagnostics(const int wkc, const int expected_wkc)
  {
    ecx_readstate(&context_.context);

    // Do a best effort try to gain access to the diagnostics mutex
    // if it doesn't work we just life with it to avoid timing issues
    if (!diagnostics_mutex_.try_lock()) {
      return;
    }
    DiagnosticsSnapshot snap = latest_diagnostics_;
    diagnostics_mutex_.unlock();

    // Update the internal diagnostics with:
    snap.timestamp = HighPrecisionClock::now();

    // Case a slave did not answer
    if (wkc >= 0 && wkc != expected_wkc) {
      snap.bus.wkc_mismatches += 1;
    }
    // Case a frame got lost or timeout or whatever
    if (wkc < 0) {
      snap.bus.frames_lost += 1;
    }
    // Simply count up how many frames we actually sent so far (PDO only)
    snap.bus.frames_sent += 1;

    if (snap.slaves.size() != static_cast<std::size_t>(context_.ecatSlavecount_)) {
      snap.slaves.resize(static_cast<std::size_t>(context_.ecatSlavecount_));
    }

    // in round robin
    if (current_selected_diagnostics_slave_ >= static_cast<std::size_t>(context_.ecatSlavecount_)) {
      current_selected_diagnostics_slave_ = 0;
    }
    /*update_slave_port_diagnostics(context_.ecatSlavelist_[current_selected_diagnostics_slave_ + 1].configadr,
                                  snap.slaves[current_selected_diagnostics_slave_]);
    current_selected_diagnostics_slave_ += 1;

    // now we use the cache data we have available
    for (uint16_t i = 0; i < static_cast<uint16_t>(context_.ecatSlavecount_); i++) {
      snap.slaves[i].position = i + 1;
      snap.slaves[i].al_status = context_.ecatSlavelist_[i + 1].ALstatuscode;
      snap.slaves[i].state =
          map_from_soem_device_state(static_cast<ec_state>(context_.ecatSlavelist_[i + 1].state & 0x0F));
      snap.slaves[i].online = !context_.ecatSlavelist_[i + 1].islost;
    }*/

    {
      std::lock_guard lock(diagnostics_mutex_);
      latest_diagnostics_ = std::move(snap);
    }
  }

  void update_slave_port_diagnostics(const uint16 config_adr, ESCStatus& status)
  {
    constexpr uint16_t DL_STATUS = 0x0110;          // 2 bytes: link/loop/comm status
    constexpr uint16_t RX_ERROR_COUNTER = 0x0300;   // 8 bytes: 2 per port (invalid, rx err) x4
    constexpr uint16_t LOST_LINK_COUNTER = 0x0310;  // 4 bytes: 1 per port x4
    uint16_t dl_status = 0;
    ecx_FPRD(&context_.ecat_port, config_adr, DL_STATUS, sizeof(dl_status), &dl_status, EC_TIMEOUTRET);

    uint8_t rx_err[8] = {};
    ecx_FPRD(&context_.ecat_port, config_adr, RX_ERROR_COUNTER, sizeof(rx_err), rx_err, EC_TIMEOUTRET);

    uint8_t lost_link[4] = {};
    ecx_FPRD(&context_.ecat_port, config_adr, LOST_LINK_COUNTER, sizeof(lost_link), lost_link, EC_TIMEOUTRET);

    for (std::size_t p = 0; p < 4; ++p) {
      // Physical link bits per port sit at bits 4-7 of DL Status.
      // VERIFY bit layout against your ESC datasheet.
      status.ports[p].link_up = (dl_status & (1u << (4 + p))) != 0;
      status.ports[p].invalid_frames = rx_err[p * 2];
      status.ports[p].rx_errors = rx_err[p * 2 + 1];
      status.ports[p].lost_links = lost_link[p];
    }
  }
};

// Pimpl - redirections
EthercatBus::EthercatBus(const Parameters& params)
{
  impl_ = std::make_unique<EthercatBus::BackendImpl>(params);
}
EthercatBus::~EthercatBus()
{
}
int EthercatBus::initialize()
{
  return impl_->initialize();
}
std::optional<std::chrono::nanoseconds> EthercatBus::update()
{
  return impl_->update();
}
const EthercatBus::Parameters& EthercatBus::get_parameters() const
{
  return impl_->get_parameters();
}
void EthercatBus::attach_device(const DeviceId device_id, std::shared_ptr<EthercatDeviceBase> device)
{
  impl_->attach_device(device_id, device);
  // Important: As the impl_ does not know the bus we need to perform the configure step here
  const auto scan_result = scan(device_id);
  device->configure(this, scan_result);
}
bool EthercatBus::has_device(const DeviceId device_id) const
{
  return impl_->has_device(device_id);
}
bool EthercatBus::has_device_on_bus(const DeviceId device_id) const
{
  return impl_->has_device_on_bus(device_id);
}

SDOReadResult EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                            const SDOSubIndex sub_index, bool check_size)
{
  return impl_->read_sdo_untyped(data, device_id, index, sub_index, check_size);
}
void EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index, const SDOReadCallback& cb, bool check_size)
{
  impl_->read_sdo_untyped_async(cb, data, device_id, index, sub_index, check_size);
}
SDOWriteResult EthercatBus::write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                              const SDOIndex index, const SDOSubIndex sub_index)
{
  return impl_->write_sdo_untyped(data, device_id, index, sub_index);
}
void EthercatBus::write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                    const SDOSubIndex sub_index, const SDOWriteCallback& cb)
{
  impl_->write_sdo_untyped_async(cb, data, device_id, index, sub_index);
}

ObjectDictionary EthercatBus::read_od(const DeviceId device_id, bool full_read)
{
  return ObjectDictionary{ impl_->read_od_from_device(device_id, full_read) };
}

std::vector<DeviceInfo> EthercatBus::scan()
{
  return impl_->scan();
}
void EthercatBus::startup()
{
  impl_->startup();
}
void EthercatBus::activate()
{
  impl_->activate();
}
void EthercatBus::shutdown()
{
  impl_->shutdown();
}

std::vector<std::string> EthercatBus::list_interfaces()
{
  ec_adaptert* adapter = nullptr;

  adapter = ec_find_adapters();
  if (!adapter)
    throw std::runtime_error("Error calling ec_find_adapters");

  std::vector<std::string> result;

  auto iter = adapter;
  while (iter != nullptr) {
    result.push_back(iter->name);
    iter = iter->next;
  }

  ec_free_adapters(adapter);
  return result;
}
DeviceInfo EthercatBus::scan(const DeviceId device_id)
{
  return impl_->scan(device_id);
}

std::vector<uint8_t> EthercatBus::read_rx_pdo(const DeviceId device_id) const
{
  return impl_->read_rx_pdo(device_id);
}

void EthercatBus::write_rx_pdo(const DeviceId device_id, const std::vector<uint8_t>& data)
{
  impl_->write_rx_pdo(device_id, data);
}

std::vector<uint8_t> EthercatBus::read_tx_pdo(const DeviceId device_id) const
{
  return impl_->read_tx_pdo(device_id);
}

template <typename T>
std::optional<T> EthercatBus::sdo_read(const DeviceId device_id, const SDOIndex index, const SDOSubIndex sub_index)
{
  static_assert(!std::is_same_v<T, std::string>);
  // This is the actual data instance we use
  T data{};
  // And this is just a safe representation (pointer + size) to it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(&data), sizeof(T));

  if (!read_sdo_untyped(buffer, device_id, index, sub_index)) {
    return std::nullopt;
  }

  return data;
}

template <>
std::optional<std::string> EthercatBus::sdo_read<std::string>(const DeviceId device_id, const SDOIndex index,
                                                              const SDOSubIndex sub_index)
{
  // Create a buffer of the maximum possible string length
  std::vector<char> data(maximum_visible_string_size, 0);
  // Create a uin8t_t span reprensentation of it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(data.data()), data.size());
  const auto result =
      read_sdo_untyped(buffer, device_id, index, sub_index, false /*do not perform size check for string reads*/);
  if (!result) {
    // Something during the read failed
    return std::nullopt;
  }

  // Create an std::string out of it
  return std::string(data.begin(), data.begin() + result.actual_size_read);
}

template std::optional<bool> EthercatBus::sdo_read<bool>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<uint8_t> EthercatBus::sdo_read<uint8_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<int8_t> EthercatBus::sdo_read<int8_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<uint16_t> EthercatBus::sdo_read<uint16_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<int16_t> EthercatBus::sdo_read<int16_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<uint32_t> EthercatBus::sdo_read<uint32_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<int32_t> EthercatBus::sdo_read<int32_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<uint64_t> EthercatBus::sdo_read<uint64_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<int64_t> EthercatBus::sdo_read<int64_t>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<float> EthercatBus::sdo_read<float>(DeviceId, SDOIndex, SDOSubIndex);
template std::optional<double> EthercatBus::sdo_read<double>(DeviceId, SDOIndex, SDOSubIndex);

template <typename T>
bool EthercatBus::sdo_write(const DeviceId device_id, const T value, const SDOIndex index, const SDOSubIndex sub_index)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
  const auto result = write_sdo_untyped(data, device_id, index, sub_index);
  if (!result) {
    // Something during the write failed
    return false;
  }

  return true;
}

template <>
bool EthercatBus::sdo_write<std::string>(const DeviceId device_id, const std::string value, const SDOIndex index,
                                         const SDOSubIndex sub_index)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  const auto result = write_sdo_untyped(data, device_id, index, sub_index);
  if (!result) {
    // Something during the write failed
    return false;
  }

  return true;
}

template bool EthercatBus::sdo_write<bool>(DeviceId, const bool, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<uint8_t>(DeviceId, const uint8_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<int8_t>(DeviceId, const int8_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<uint16_t>(DeviceId, const uint16_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<int16_t>(DeviceId, const int16_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<uint32_t>(DeviceId, const uint32_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<int32_t>(DeviceId, const int32_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<uint64_t>(DeviceId, const uint64_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<int64_t>(DeviceId, const int64_t, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<float>(DeviceId, const float, SDOIndex, SDOSubIndex);
template bool EthercatBus::sdo_write<double>(DeviceId, const double, SDOIndex, SDOSubIndex);

FoEWriteResult EthercatBus::foe_write(const DeviceId device_id, const std::string& file_name,
                                      std::span<const uint8_t> data)
{
  return impl_->foe_write(device_id, file_name, data);
}
FoEReadResult EthercatBus::foe_read(const DeviceId device_id, const std::string& file_name, std::span<uint8_t> buffer)
{
  return impl_->foe_read(device_id, file_name, buffer);
}

bool EthercatBus::change_device_state(const DeviceId device_id, const EthercatDeviceState target_state, bool blocking)
{
  // Convert into soem internal type
  const auto soem_state = map_to_soem_device_state(target_state);
  // Request the state change
  bool res = impl_->set_device_target_state(device_id, soem_state);

  // Handle the case that the user does not want to block or that the set command failed
  if (!blocking || !res) {
    return res;
  }

  return impl_->wait_for_device_target_state(device_id, soem_state);
}

RegisterReadResult EthercatBus::read_register_untyped(std::span<uint8_t> data, const DeviceId device_id,
                                                      const RegisterAddress address, bool check_size)
{
  return impl_->read_register_untyped(data, device_id, address, check_size);
}

RegisterWriteResult EthercatBus::write_register_untyped(std::span<const uint8_t> data, const DeviceId device_id,
                                                        const RegisterAddress address)
{
  return impl_->write_register_untyped(data, device_id, address);
}

template <typename T>
std::optional<T> EthercatBus::register_read(const DeviceId device_id, const RegisterAddress address, bool check_size)
{
  // This is the actual data instance we use
  T data{};
  // And this is just a safe representation (pointer + size) to it
  std::span<uint8_t> buffer(reinterpret_cast<uint8_t*>(&data), sizeof(T));

  if (!read_register_untyped(buffer, device_id, address, check_size)) {
    return std::nullopt;
  }
  return data;
}

template std::optional<bool> EthercatBus::register_read<bool>(DeviceId, RegisterAddress, bool);
template std::optional<uint8_t> EthercatBus::register_read<uint8_t>(DeviceId, RegisterAddress, bool);
template std::optional<int8_t> EthercatBus::register_read<int8_t>(DeviceId, RegisterAddress, bool);
template std::optional<uint16_t> EthercatBus::register_read<uint16_t>(DeviceId, RegisterAddress, bool);
template std::optional<int16_t> EthercatBus::register_read<int16_t>(DeviceId, RegisterAddress, bool);
template std::optional<uint32_t> EthercatBus::register_read<uint32_t>(DeviceId, RegisterAddress, bool);
template std::optional<int32_t> EthercatBus::register_read<int32_t>(DeviceId, RegisterAddress, bool);
template std::optional<uint64_t> EthercatBus::register_read<uint64_t>(DeviceId, RegisterAddress, bool);
template std::optional<int64_t> EthercatBus::register_read<int64_t>(DeviceId, RegisterAddress, bool);
template std::optional<float> EthercatBus::register_read<float>(DeviceId, RegisterAddress, bool);
template std::optional<double> EthercatBus::register_read<double>(DeviceId, RegisterAddress, bool);

template <typename T>
bool EthercatBus::register_write(const DeviceId device_id, const RegisterAddress address, const T data)
{
  // Call by value makes this function safer in case the call needs to be queued
  std::span<const uint8_t> data_wrap(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
  const auto result = write_register_untyped(data_wrap, device_id, address);
  if (!result) {
    return false;
  }
  return true;
}

template bool EthercatBus::register_write<bool>(DeviceId, RegisterAddress, const bool);
template bool EthercatBus::register_write<uint8_t>(DeviceId, RegisterAddress, const uint8_t);
template bool EthercatBus::register_write<int8_t>(DeviceId, RegisterAddress, const int8_t);
template bool EthercatBus::register_write<uint16_t>(DeviceId, RegisterAddress, const uint16_t);
template bool EthercatBus::register_write<int16_t>(DeviceId, RegisterAddress, const int16_t);
template bool EthercatBus::register_write<uint32_t>(DeviceId, RegisterAddress, const uint32_t);
template bool EthercatBus::register_write<int32_t>(DeviceId, RegisterAddress, const int32_t);
template bool EthercatBus::register_write<uint64_t>(DeviceId, RegisterAddress, const uint64_t);
template bool EthercatBus::register_write<int64_t>(DeviceId, RegisterAddress, const int64_t);
template bool EthercatBus::register_write<float>(DeviceId, RegisterAddress, const float);
template bool EthercatBus::register_write<double>(DeviceId, RegisterAddress, const double);

DiagnosticsSnapshot EthercatBus::diagnostics(bool force_update)
{
  return impl_->diagnostics(force_update);
}
}  // namespace duatic::ethercat_interface
