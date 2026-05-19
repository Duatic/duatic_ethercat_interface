#include "duatic_ethercat_interface/ethercat_device.hpp"
#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface
{
EthercatDeviceBase::EthercatDeviceBase(const Hooks& hooks) : hooks_(hooks)
{
}
void EthercatDeviceBase::configure(EthercatBus* bus, DeviceInfo device_info)
{
  device_info_ = device_info;
  bus_ = bus;
  on_configure();
}

ObjectDictionary EthercatDeviceBase::read_od(bool full_read) const
{
  return bus_->read_od(get_device_id(), full_read);
}

void GenericEthercatDevice::update_write()
{
  bus_->write_rx_pdo(get_device_id(), rx_pdo_);
}
void GenericEthercatDevice::update_read()
{
  tx_pdo_ = bus_->read_tx_pdo(get_device_id());
};

}  // namespace duatic::ethercat_interface
