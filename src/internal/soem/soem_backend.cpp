#include "duatic_ethercat_interface/ethercat_bus.hpp"

#include "duatic_ethercat_interface/exceptions.hpp"
#include "duatic_ethercat_interface/precision_update_rate.hpp"

#include "duatic_ethercat_interface/internal/soem/soem_context.hpp"
#include "duatic_message_logger/log.hpp"
// Implementation of the EthercatBus for the SOEM library
namespace duatic::ethercat_interface
{

using namespace internal::soem;

enum class BusState
{
  PreInit,
  Initialized,
  Configured,
  Operational,
  Shutdown
};

struct EthercatBus::Impl
{
  Impl(const Parameters& params) : params_(params)
  {
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
    if (device_id > get_device_count()) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    const int requested_size = data.size();
    int actual_size = requested_size;
    int wkc = 0;
    if (get_bus_state() == BusState::Operational) {
      // TODO queue sdo call into update thread
    } else {
      // Directly perform the read

      wkc = ecx_SDOread(&context_.context, device_id, index, sub_index, FALSE, &actual_size, data.data(), timeout);
      if (wkc <= 0) {
        logging::error() << "Device id " << device_id << ": Working counter too low (" << wkc
                         << ") for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                         << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                         << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        return SDOReadResult{ .success = false, .actual_size_read = actual_size, .working_counter = wkc };
      }

      if (requested_size != actual_size) {
        logging::error() << "Device id  " << device_id << ": Size mismatch (expected " << requested_size
                         << " bytes, read " << actual_size << " bytes) for reading SDO (ID: 0x" << std::setfill('0')
                         << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0') << std::setw(2)
                         << std::hex << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        throw BackendError("SDORead size mismatch", Backend::SOEM, actual_size);
      }
    }
    return SDOReadResult{ .success = true, .actual_size_read = actual_size, .working_counter = wkc };
  }
  bool write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index = 0, const int timeout = EC_TIMEOUTRXM)
  {
    // Only perform operations on an initialized bus
    if (get_bus_state() == BusState::PreInit) {
      throw BackendError("Backend not initialized - cannot perform SDO operations", Backend::SOEM);
    }
    // And only on devices that are actually on the bus
    if (device_id > get_device_count()) {
      throw DeviceNotFound("Device with id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // Depending on the current bus state we need to handle SDO access differently
    // When the bus is up and running we should enqueue and SDO access into the main update thread
    // otherwise we can simply directly perform the operation
    if (get_bus_state() == BusState::Operational) {
      // TODO queue sdo call into update thread
    } else {
      // Directly perform the write
      const int wkc = ecx_SDOwrite(&context_.context, device_id, index, sub_index, FALSE, data.size(),
                                   const_cast<uint8_t*>(data.data()), timeout);
      if (wkc <= 0) {
        logging::error() << "Device id " << device_id << ": Working counter too low (" << wkc
                         << ") for writing SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                         << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                         << static_cast<uint16_t>(sub_index) << ")." << std::endl;

        return false;
      }
    }
    return true;
  }

  int get_device_count() const
  {
    return context_.ecatSlavecount_;
  }

  void add_device(std::unique_ptr<EthercatDeviceBase> device)
  {
    // As we need to perfrom the right PDO mapping we need to make sure that the bus is in the right state
    if (get_bus_state() != BusState::Initialized || get_bus_state() != BusState::Configured) {
      throw BackendError("Cannot attach device to bus - bus it not in the correct state", Backend::SOEM);
    }
    // And that we not already handle a device with this id
    if (has_device(device->get_device_id())) {
      throw BackendError("Device with id: " + std::to_string(device->get_device_id()) +
                             " is already handled by this bus",
                         Backend::SOEM);
    }
    devices_.emplace_back(std::move(device));
  }

  DeviceContext& create_device_context(EthercatBus* bus, const DeviceId device_id)
  {
    // Check if the device even is on the bus
    if (has_device_on_bus(device_id)) {
      throw BackendError("Device id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // If yes we can create new device context
    device_contexts_.emplace_back(DeviceContext{ bus, device_id });
    return device_contexts_.back();
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
    io_map_.resize(iomap_size, 0);

    ecx_config_map_group(&context_.context, io_map_.data(), 0);

    update_bus_state(BusState::Configured);
  }

  void activate()
  {
    if (get_bus_state() != BusState::Configured) {
      throw BackendError("Cannot perform activate - bus needs to be in 'Configured' state", Backend::SOEM);
    }

    // Give the device the chance to perform any last steps before we
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
  }

  void shutdown()
  {
  }

  bool has_device(const DeviceId device_id) const
  {
    return std::find_if(devices_.begin(), devices_.end(),
                        [&](const auto& d) { return d->get_device_id() == device_id; }) == devices_.end();
  }

  void update()
  {
  }

private:
  const std::string interface_;
  EthercatContext context_;
  const Parameters params_;
  BusState state_{ BusState::PreInit };

  std::vector<std::unique_ptr<EthercatDeviceBase>> devices_;
  std::vector<DeviceContext> device_contexts_;
  std::vector<uint8_t> io_map_;

  void update_bus_state(BusState state)
  {
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
    return static_cast<ec_state>(context_.ecatSlavelist_[device_id].state);
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
  impl_ = std::make_unique<EthercatBus::Impl>(params);
}
int EthercatBus::initialize()
{
  return impl_->initialize();
}
const EthercatBus::Parameters& EthercatBus::get_parameters() const
{
  return impl_->get_parameters();
}
void EthercatBus::add_device(std::unique_ptr<EthercatDeviceBase> device)
{
  impl_->add_device(std::move(device));
}
bool EthercatBus::has_device(const DeviceId device_id) const
{
  return impl_->has_device(device_id);
}
DeviceContext& EthercatBus::create_device_context(EthercatBus* bus, const DeviceId device_id)
{
  return impl_->create_device_context(bus, device_id);
}
SDOReadResult EthercatBus::read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                            const SDOSubIndex sub_index)
{
  return impl_->read_sdo_untyped(data, device_id, index, sub_index);
}
bool EthercatBus::write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                    const SDOSubIndex sub_index)
{
  return impl_->write_sdo_untyped(data, device_id, index, sub_index);
}
}  // namespace duatic::ethercat_interface
