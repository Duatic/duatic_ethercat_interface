#include "duatic_ethercat_interface/ethercat_bus.hpp"

#include <mutex>
#include <thread>

#include "duatic_ethercat_interface/exceptions.hpp"
#include "duatic_ethercat_interface/precision_update_rate.hpp"
#include "duatic_ethercat_interface/realtime_utils.hpp"
#include "duatic_ethercat_interface/object_dictionary.hpp"

#include "duatic_ethercat_interface/internal/backend_impl.hpp"
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
  Activated,
  Operational,
  Shutdown
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
    : params_(params), update_rate_(params_.update_rate), logger_(logging::get_logger_with_default_sink("SOEM-Backend"))
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
        logging::error(logger_) << "Device id " << device_id << ": Working counter too low (" << wkc
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
    if (iomap_size < 0) {
      throw BackendError("IOMap size < 0 (" + std::to_string(iomap_size) + ")", Backend::SOEM);
    }
    io_map_.resize(static_cast<std::size_t>(iomap_size), 0);
    // And then do the actual configuration
    const int final_iomap_size = ecx_config_map_group(&context_.context, io_map_.data(), 0);
    logging::info(logger_) << "IOMap size: " << final_iomap_size << std::endl;
    ecx_configdc(&context_.context);
    update_bus_state(BusState::Configured);
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

    // If configured we now start the ethercat update thread
    if (params_.update_mode == UpdateMode::Synchronous) {
      update_thread_ = std::jthread([this](std::stop_token st) {
        // Configure rt-prio for this task
        set_realtime_priority(params_.realtime_priority, params_.desired_cpu_core);
        while (!st.stop_requested()) {
          this->update();
        }
      });
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
        update_bus_state(BusState::Operational);
      }
    }

    std::lock_guard<std::mutex> lock(update_mutex_);
    if (params_.update_mode == UpdateMode::Synchronous) {
      if (!update_rate_.step()) {
        logging::warning(logger_) << "Update took too long: " << update_rate_.accumulated_delay_ns() << std::endl;
      }
    }
  }

  std::vector<SDOEntry> read_od_from_device(const DeviceId device_id, bool full_read)
  {
    std::vector<SDOEntry> result;

    ec_ODlistt od_list{};

    // 1. Read Object Dictionary list (top level objects)
    if (!ecx_readODlist(&context_.context, device_id, &od_list)) {
      throw BackendError("Failed to read OD list from device", Backend::SOEM);
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
  std::vector<std::unique_ptr<EthercatDeviceBase>> devices_;
  std::vector<DeviceContext> device_contexts_;

  // Everything update thread related
  std::mutex update_mutex_;
  std::jthread update_thread_;
  PrecisionUpdateRate update_rate_;

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
      // We take the data in the device and copy it to the corresponding device memory
      auto& access_wrapper = device->access_tx_pdo();

      // As this piece of memory can be access by multiple threads we need to lock it
      std::lock_guard<std::mutex> lock(access_wrapper.access_lock);

      // pdo size check
      const auto target_size = context_.ecatSlavelist_[device->get_device_id()].Obytes;
      const auto source_size = access_wrapper.raw.size_bytes();

      if (target_size != source_size) {
        logging::error(logger_) << "Device: " << device->get_device_id()
                                << " pdo size (tx) does not match device pdo size: " << source_size << " vs "
                                << target_size << std::endl;
      }
      // Copy it around
      std::memcpy(context_.ecatSlavelist_[device->get_device_id()].outputs, access_wrapper.raw.data(), target_size);
    }
    if (ecx_send_processdata(&context_.context) <= 0) {
      logging::error(logger_) << "Failed to send process data" << std::endl;
    }
    const int wkc = ecx_receive_processdata(&context_.context, EC_TIMEOUTRET);

    const int expected_wkc = context_.context.grouplist[0].outputsWKC * 2 + context_.context.grouplist[0].inputsWKC;
    if (wkc < expected_wkc) {
      logging::warning(logger_) << params_.interface << " Working counter too low: " << wkc
                                << " expected wkc: " << expected_wkc << std::endl;
    }
    // Update the pdo state of every device
    for (auto& device : devices_) {
      auto& access_wrapper = device->access_rx_pdo();
      std::lock_guard<std::mutex> lock(access_wrapper.access_lock);

      const auto source_size = context_.ecatSlavelist_[device->get_device_id()].Ibytes;
      const auto target_size = access_wrapper.raw.size_bytes();
      if (target_size != source_size) {
        logging::error(logger_) << "Device: " << device->get_device_id()
                                << " pdo size (rx) does not match device pdo size: " << source_size << " vs "
                                << target_size << std::endl;
      }
      std::memcpy(access_wrapper.raw.data(), context_.ecatSlavelist_[device->get_device_id()].inputs, target_size);
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
  if (impl_->get_parameters().update_mode == UpdateMode::Synchronous) {
    return;
  }
  impl_->update();
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

}  // namespace duatic::ethercat_interface
