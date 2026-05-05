#include "duatic_ethercat_interface/internal/soem_backend.hpp"

#include <soem_vendor/ethercat.h>

namespace duatic::ethercat_interface::internal
{

void SOEMBusImpl::add_device(std::shared_ptr<EthercatDeviceBase> d)
{
  devices.push_back(d);
}

void SOEMBusImpl::init()
{
  ec_init("eth0");
}

void SOEMBusImpl::scan()
{
}

void SOEMBusImpl::configure()
{
  // SOEM-specific PDO setup
}

void SOEMBusImpl::activate()
{
}

void SOEMBusImpl::cycle()
{
}

}  // namespace duatic::ethercat_interface::internal
