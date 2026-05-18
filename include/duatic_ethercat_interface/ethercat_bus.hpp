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
  // Update is done externaly
  Synchronous,
  // Update is handled by the bus itself
  SelfManaged
};

// NOTE all non realtime critical functions can throw
class EthercatBus
{
public:
  struct Parameters
  {
    // Name of the ethernet interface
    std::string interface;
    // Update step rate in nanoseconds (default 1kHz)
    std::chrono::nanoseconds update_rate{ 1000000 };

    // In case of UpdateMode::SelfManaged priority and desired cpu core for the update thread
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
  DeviceInfo scan(const DeviceId device_id);

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
  void spin();

  void attach_device(const DeviceId device_id, std::shared_ptr<EthercatDeviceBase> device);
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
   * @brief read_rx_pdo - obtain raw pdo data of the rx pdo (rx -> direction the device receives)
   */
  std::vector<uint8_t> read_rx_pdo( const DeviceId device_id) const;
  /**
   * @brief write_rx_pdo - write raw data of the rx pdo (rx -> direction the device receives)
   */
  void write_rx_pdo( const DeviceId device_id,const std::vector<uint8_t>& data);
  /**
   * @brief read_tx_pdo - read raw data of the tx pdo (tx -> direction the device transmits)
   */
  std::vector<uint8_t> read_tx_pdo( const DeviceId device_id) const;


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
