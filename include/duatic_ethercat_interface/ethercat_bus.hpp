#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <span>
#include <chrono>

#include "duatic_ethercat_interface/ethercat_device.hpp"
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

    int realtime_priority{ 60 };
    int desired_cpu_core{ -1 };
  };

  explicit EthercatBus(const Parameters& params);
  virtual ~EthercatBus();

  /**
   * @brief initialize the bus
   * @throws BackendError
   * @return Amount of slaves found on the bus
   */
  int initialize();

  std::vector<DeviceInfo> scan();
  ObjectDictionary read_od(const DeviceId device_id, bool full_read = false);

  /**
   * @brief startup - next step after initialize
   * This configures the PDO mapping of all devices on the bus
   * @note afterwards no additional device can be added
   */
  void startup();
  /**
   * @brief activate - next step after startup
   * This brings all devices into SAFE_OP mode
   * @note on UpdateMode::Synchronous configuration the bus will automatically bring all devices into Operational mode
   * and peform the cyclic update
   * @note on UpdateMode::SelfManaged the user must call update() himself with the desired update rate
   */
  void activate();
  /**
   * @brief shutdown - perform a safe shutdown of the bus
   */
  void shutdown();

  /**
   * @brief update - perform a single bus update step
   * @note in case UpdateMode::Synchronous was used this method does nothing
   */
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
    auto device = std::make_unique<T>(this, device_id);
    T* raw_device = device.get();
    add_device(std::move(device));
    // We return a NON-OWNING pointer to the device
    return raw_device;
  }
  /**
   * @brief get_parameters - obtain the configuration objects
   * @return const reference to used Paramters
   */
  const Parameters& get_parameters() const;

  /**
   * @brief Check if the bus manages a specific device
   * @param device_id - the id (bus address) of the device you are looking for
   * @return true in case the bus manages that specifc device
   */
  bool has_device(const DeviceId device_id) const;

  // Untyped functions for reading writing SDOs
  // Blocking
  SDOReadResult read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                 const SDOSubIndex sub_index);
  bool write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index);
  // Non-blocking
  bool read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                        const SDOSubIndex sub_index, const SDOReadCallback& cb);
  bool write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index, const SDOWriteCallback& cb);

  /**
   * @brief list_interface - provide a list with all supported interface names
   * @return list of strings of the interface names
   */
  static std::vector<std::string> list_interfaces();

private:
  // Pimpl pattern which hides the actual backend implementation
  class BackendImpl;
  std::unique_ptr<BackendImpl> impl_;

  // We only accept devices that we also allocated ourselves
  void add_device(std::unique_ptr<EthercatDeviceBase> device);
};

}  // namespace duatic::ethercat_interface
