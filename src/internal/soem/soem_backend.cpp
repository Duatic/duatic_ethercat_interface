#include "duatic_ethercat_interface/ethercat_bus.hpp"

#include "duatic_ethercat_interface/exceptions.hpp"

#include "duatic_ethercat_interface/internal/soem/soem_context.hpp"
#include <iostream>
// Implementation of the EthercatBus for the SOEM library
namespace duatic::ethercat_interface
{

using namespace internal::soem;

enum class BusState
{
  PreInit,
  Initialized,
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

  bool read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
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
    if (get_bus_state() == BusState::Operational) {
      // TODO queue sdo call into update thread
    } else {
      // Directly perform the read
      const int requested_size = data.size();
      int actual_size = requested_size;

      const int wkc =
          ecx_SDOread(&context_.context, device_id, index, sub_index, FALSE, &actual_size, data.data(), timeout);
      if (wkc <= 0) {
        std::cerr << "Device id " << device_id << ": Working counter too low (" << wkc << ") for reading SDO (ID: 0x"
                  << std::setfill('0') << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0')
                  << std::setw(2) << std::hex << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        return false;
      }

      if (requested_size != actual_size) {
        std::cerr << "Device id  " << device_id << ": Size mismatch (expected " << requested_size << " bytes, read "
                  << actual_size << " bytes) for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex
                  << index << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                  << static_cast<uint16_t>(sub_index) << ")." << std::endl;
        throw BackendError("SDORead size mismatch", Backend::SOEM, actual_size);
      }
    }

    return true;
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
        std::cerr << "Device id " << device_id << ": Working counter too low (" << wkc << ") for writing SDO (ID: 0x"
                  << std::setfill('0') << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0')
                  << std::setw(2) << std::hex << static_cast<uint16_t>(sub_index) << ")." << std::endl;

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
    
    if (has_device(device->get_device_id())) {
      throw BackendError("Device with id: " + std::to_string(device->get_device_id()) +
                             " is already handled by this bus",
                         Backend::SOEM);
    }
    devices_.emplace_back(std::move(device));
  }
  bool has_device(const DeviceId device_id) const
  {
    return std::find_if(devices_.begin(), devices_.end(),
                        [&](const auto& d) { return d->get_device_id() == device_id; }) == devices_.end();
  }
  DeviceContext& create_device_context(EthercatBus* bus, const DeviceId device_id)
  { 
    // Check if the device even is on the bus
    if(has_device_on_bus(device_id)){
      throw BackendError("Device id: " + std::to_string(device_id) + " not found on the bus", Backend::SOEM);
    }
    // If yes we can create new device context
     device_contexts_.emplace_back(DeviceContext{ bus, device_id });
     return device_contexts_.back();
  }

  void startup() {
    for(auto & device: devices_){
      device->on_startup();
    }
  }

private:
  const std::string interface_;
  EthercatContext context_;
  const Parameters params_;
  BusState state_{ BusState::PreInit };

  std::vector<std::unique_ptr<EthercatDeviceBase>> devices_;
  std::vector<DeviceContext> device_contexts_;

  void update_bus_state(BusState state)
  {
    state_ = state;
  }
  BusState get_bus_state() const
  {
    return state_;
  }

  /**
   * @brief check if the specific device has been found on the bus
   * @note this is different to "has_device" which checks if we handle a specific device
   */
  bool has_device_on_bus(const DeviceId id) const {
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
  return impl_->create_device_context(bus,device_id);
}
}  // namespace duatic::ethercat_interface
