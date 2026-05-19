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

#include <mutex>   // NOLINT(build/include_order)
#include <thread>  // NOLINT(build/include_order)
#include <queue>   // NOLINT(build/include_order)
#include <atomic>  // NOLINT(build/include_order)

#include "duatic_ethercat_interface/exceptions.hpp"
#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/precision_update_rate.hpp"

#include "duatic_ethercat_interface/object_dictionary.hpp"

#include "duatic_ethercat_interface/internal/backend_impl.hpp"
#include "duatic_ethercat_interface/internal/soem/soem_context.hpp"
#include "duatic_message_logger/log.hpp"
// Implementation of the EthercatBus for the SOEM library
namespace duatic::ethercat_interface
{

using namespace internal::soem;  // NOLINT(build/namespaces)

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

struct EthercatBus::BackendImpl
{
  explicit BackendImpl(const Parameters& params)
    : params_(params), logger_(logging::get_logger_with_default_sink("SOEM-Backend"))
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
                                 const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
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
      if (wkc <= 0) {
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                                << ") for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                                << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                                << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        return SDOReadResult{ .success = false, .actual_size_read = actual_size, .working_counter = wkc };
      }

      if (requested_size != actual_size) {
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
                              const SDOIndex index, const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
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
                            .transfer_finished_cb = [&](const bool success, const int actual_size, const int wkc) {
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
      if (wkc <= 0) {
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
                                << ") for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                                << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                                << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        result = SDOReadResult{ .success = false, .actual_size_read = actual_size, .working_counter = wkc };
      } else {
        result = SDOReadResult{ .success = true, .actual_size_read = actual_size, .working_counter = wkc };
      }

      if (requested_size != actual_size) {
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
                            .direction = SDOTransfer::Direction::Read,
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
      if (wkc <= 0) {
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
                            .direction = SDOTransfer::Direction::Read,
                            .data = const_cast<uint8_t*>(data.data()),  // TODO introduce write/read transfers?
                            .data_size = static_cast<int>(data.size()),
                            .timeout = timeout,
                            .transfer_finished_cb = [&](const bool success, [[maybe_unused]] const int actual_size,
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
      if (wkc <= 0) {
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
    if (get_bus_state() != BusState::Initialized && get_bus_state() != BusState::Configured) {
      throw BackendError("Cannot attach device to bus - bus it not in the correct state", Backend::SOEM);
    }
    // Check if the device even is on the bus
    if (has_device_on_bus(device_id)) {
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

    // Perform PDO setup
    // First determine the necessary io map size
    const int iomap_size = ecx_config_map_group(&context_.context, NULL, 0);
    if (iomap_size < 0) {
      throw BackendError("IOMap size < 0 (" + std::to_string(iomap_size) + ")", Backend::SOEM);
    }
    io_map_.resize(static_cast<std::size_t>(iomap_size), 0);
    // And then do the actual configuration
    const int final_iomap_size = ecx_config_map_group(&context_.context, io_map_.data(), 0);
    logging::info(logger_) << "IOMap size: " << final_iomap_size << std::endl;
    ecx_configdc(&context_.context);
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
      if (wait_for_device_target_state(device->get_device_id(), ec_state::EC_STATE_SAFE_OP)) {
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

    if (get_bus_state() == BusState::Configured || get_bus_state() == BusState::Operational) {
      // Bring all devices into pre-op
      // Note: using device id 0 targets all devices on the bus
      set_device_target_state(0, ec_state::EC_STATE_PRE_OP);
      wait_for_device_target_state(0, ec_state::EC_STATE_PRE_OP);
      update_bus_state(BusState::Shutdown);
      return;
    }

    for (auto& device : devices_) {
      device->on_post_shutdown();
    }

    throw BackendError("Invalid BusState in shutdown - dont know what to do", Backend::SOEM);
  }

  bool has_device(const DeviceId device_id) const
  {
    return std::find_if(devices_.begin(), devices_.end(),
                        [&](const auto& d) { return d->get_device_id() == device_id; }) == devices_.end();
  }

  void update()
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
    internal_pdo_update();

    {
      std::lock_guard<std::mutex> lock(sdo_update_mutex_);
      // Check if the sdo transfer queue is not empty and handle 1 transfer
      if (!sdo_transfer_queue_.empty()) {
        auto transfer = sdo_transfer_queue_.front();
        sdo_transfer_queue_.pop();

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
    // TODO lock

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
    // TODO LOCK
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
    // TODO LOCK
    std::memcpy(result.data(), context_.ecatSlavelist_[device_id].inputs, size);
    return result;
  }

private:
  // Parameterization
  const std::string interface_;
  const Parameters params_;

  // Ethercat context and bus state tracking
  EthercatContext context_;
  // Memory where SOEM will store its pdos aftwards
  std::vector<uint8_t> io_map_;
  BusState state_{ BusState::PreInit };

  // Device management
  std::vector<std::shared_ptr<EthercatDeviceBase>> devices_;

  // Everything update thread related
  std::mutex sdo_update_mutex_;
  std::mutex pdo_update_mutex_;

  // Synchronization of sdo read/writes into update thread
  std::queue<SDOTransfer> sdo_transfer_queue_;

  logging::Logger logger_;

  void update_bus_state(BusState state)
  {
    logging::info(logger_) << "Bus transitioning into state: " << state;
    state_ = state;
  }
  BusState get_bus_state() const
  {
    return state_;
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

  void internal_pdo_update()
  {
    // Take the latest tx pdo state from every device
    for (auto& device : devices_) {
      device->update_write();
    }
    {  // Important to lock after update_write(), otherwise update_write will deadlock
      std::lock_guard<std::mutex> lock(pdo_update_mutex_);
      if (ecx_send_processdata(&context_.context) <= 0) {
        logging::error(logger_) << "Failed to send process data" << std::endl;
      }
      const int wkc = ecx_receive_processdata(&context_.context, EC_TIMEOUTRET);

      const int expected_wkc = context_.context.grouplist[0].outputsWKC * 2 + context_.context.grouplist[0].inputsWKC;
      if (wkc < expected_wkc) {
        logging::warning(logger_) << params_.interface << " Working counter too low: " << wkc
                                  << " expected wkc: " << expected_wkc << std::endl;
      }
    }  // Important to unlock here, otherwise update_read() will deadlock
    // Update the pdo state of every device
    for (auto& device : devices_) {
      device->update_read();
    }
  }

  /**
   * @brief check if the specific device has been found on the bus
   * @note this is different to "has_device" which checks if we handle a specific device
   */
  bool has_device_on_bus(const DeviceId id) const
  {
    return id < get_device_count();
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
void EthercatBus::update()
{
  // Only forward in case of self managed update
  impl_->update();
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

SDOReadResult EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                            const SDOSubIndex sub_index)
{
  return impl_->read_sdo_untyped(data, device_id, index, sub_index);
}
void EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index, const SDOReadCallback& cb)
{
  impl_->read_sdo_untyped_async(cb, data, device_id, index, sub_index);
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
  const auto result = read_sdo_untyped(buffer, device_id, index, sub_index);
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

}  // namespace duatic::ethercat_interface
