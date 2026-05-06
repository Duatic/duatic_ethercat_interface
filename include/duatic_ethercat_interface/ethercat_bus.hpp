#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <span>
#include <chrono>

#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/device_context.hpp"
#include "duatic_ethercat_interface/types.hpp"

namespace duatic::ethercat_interface
{

enum class UpdateMode
{
  Synchronous,
  SelfManaged
};

// NOTE all non realtime critical functions can throw
class EthercatBus
{
public:
  struct Parameters
  {
    std::string interface;
    std::chrono::nanoseconds update_rate{ 1000000 };
    UpdateMode update_mode{ UpdateMode::SelfManaged };
  };

  explicit EthercatBus(const Parameters& params);

  /**
   * @brief initialize the bus
   * @throws BackendError
   * @return Amount of slaves found on the bus
   */
  int initialize();

  void startup();
  void activate();
  void shutdown();

  void update();

  template <typename T>
  T* allocate_device(const DeviceId device_id)
  {
    // This is a non straight forward pattern but makes sense to bring in some allocation safety
    // We seperate context creation and device creation because we cannot pass the template to the implemenation
    // so the actual type "T" is only know
    if (has_device(device_id)) {
      throw std::runtime_error("Device is already handled by bus");
    }
    // We request the backend to create new device context for the device
    // and create the actual device and give it to the backend which also has the ownership
    auto device = std::make_unique<T>(create_device_context(this, device_id));
    T* raw_device = device.get();
    add_device(std::move(device));
    // We return a NON-OWNING pointer to the device
    return raw_device;
  }

  const Parameters& get_parameters() const;

  bool has_device(const DeviceId device_id) const;

protected:
  class Impl;
  std::unique_ptr<Impl> impl_;

  void add_device(std::unique_ptr<EthercatDeviceBase> device);
  DeviceContext& create_device_context(EthercatBus* bus, const DeviceId device_id);
  // Untyped functions for reading writing SDOs
  // Blocking
  bool read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                        const SDOSubIndex sub_index = 0);
  bool write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index = 0);
  // Non-blocking
  bool read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                        const SDOSubIndex sub_index = 0, const SDOReadCallback& cb = {});
  bool write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index = 0, const SDOWriteCallback& cb = {});


};

}  // namespace duatic::ethercat_interface
