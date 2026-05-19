#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <span>
#include <chrono>
#include <string>

#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/types.hpp"

namespace duatic::ethercat_interface
{

// NOTE all non realtime critical functions can throw
class EthercatBus
{
public:
  struct Parameters
  {
    // Name of the ethernet interface
    std::string interface;
  };

  explicit EthercatBus(const Parameters& params);
  virtual ~EthercatBus();

  /**
   * @brief initialize the bus
   * @throws BackendError
   * @return Amount of slaves found on the bus
   */
  int initialize();

  /**
   * @brief scan - perform a full scan of the configured bus and return a list of all found devices
   * @note not thread safe
   */
  std::vector<DeviceInfo> scan();
  /**
   * @brief scan - check if a specific device is on the bus and provide the full information about that bus
   * @throws DeviceNotFound in case device was not found on the bus
   * @note thread safe
   */
  DeviceInfo scan(const DeviceId device_id);
  /**
   * @brief read_od - try to read the full object dictionary from the specified device
   * @param device_id - bus id of the device to read the od from
   * @param full_read - if specified yes also all subindices are read
   * @throws DeviceNotFound in case device was not found on the bus
   * @note not thread safe
   */
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

  /**
   * @brief attach_device - let this bus instance handle the specific ethercat device.
   * Internally it will configure the device with the specific id
   */
  void attach_device(const DeviceId device_id, std::shared_ptr<EthercatDeviceBase> device);
  /**
   * @brief get_parameters - obtain the configuration objects
   * @return const reference to used Paramters
   */
  const Parameters& get_parameters() const;

  /**
   * @brief Check if the bus manages a specific device
   * @param device_id - the id (bus address) of the device you are looking for
   * @return true in case the bus manages that specific device
   * @note this checks if the device is _managed_ by this bus instance object (has been added via attach device)
   */
  bool has_device(const DeviceId device_id) const;

  /**
   * @brief Check if the specific device is found on the bus
   * @param device_id - the id (bus address) of the device you are looking for
   * @return true in case the specific device was found on the bus
   * @note this checks if the device is on the physical bus. Not if it is managed by this bus instance object
   */
  bool has_device_on_bus(const DeviceId device_id) const;

  // Untyped functions for reading writing SDOs
  // Blocking
  /**
   * @brief read_sdo_untyped - perfrom a blocking read for the specified sdo into the given buffer
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note you can also read from devices which have not been added via "attach_device"
   */
  SDOReadResult read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                 const SDOSubIndex sub_index);
  /**
   * @brief write_sdo_untyped - pefrom a blocking write to the specified sdo from the given buffer
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note you can also write to devices which have not been added via "attach_device"
   */
  SDOWriteResult write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                                   const SDOSubIndex sub_index);
  // Non-blocking
  /**
   * @brief read_sdo_untyped - perform a nonblock read to the specified sdo to the given buffer. On finish the callback
   * will be called
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note In case the bus is running the callback will be called from a different thread than the one you used for
   * calling this function
   * TODO improve callback threading model
   */
  void read_sdo_untyped(std::span<uint8_t> data, const DeviceId device_id, const SDOIndex index,
                        const SDOSubIndex sub_index, const SDOReadCallback& cb);
  /**
   * @brief write_sdo_untyped - perform a nonblock write to the specified sdo to the given buffer. On finish the
   * callback will be called
   * @throws DeviceNotFound if the device is not on the bus
   * @throws BackendError if the bus is not initialized yet
   * @note thread safe
   * @note In case the bus is running the callback will be called from a different thread than the one you used for
   * calling this function
   * TODO improve callback threading model
   */
  void write_sdo_untyped(std::span<const uint8_t> data, const DeviceId device_id, const SDOIndex index,
                         const SDOSubIndex sub_index, const SDOWriteCallback& cb);
  /**
   * @brief read_rx_pdo - obtain raw pdo data of the rx pdo (rx -> direction the device receives)
   */
  std::vector<uint8_t> read_rx_pdo(const DeviceId device_id) const;
  /**
   * @brief write_rx_pdo - write raw data of the rx pdo (rx -> direction the device receives)
   */
  void write_rx_pdo(const DeviceId device_id, const std::vector<uint8_t>& data);
  /**
   * @brief read_tx_pdo - read raw data of the tx pdo (tx -> direction the device transmits)
   */
  std::vector<uint8_t> read_tx_pdo(const DeviceId device_id) const;

  /**
   * @brief list_interface - provide a list with all supported interface names
   * @return list of strings of the interface names
   */
  static std::vector<std::string> list_interfaces();

private:
  // Pimpl pattern which hides the actual backend implementation
  class BackendImpl;
  std::unique_ptr<BackendImpl> impl_;
};

}  // namespace duatic::ethercat_interface
