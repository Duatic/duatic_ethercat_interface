#include "duatic_ethercat_interface/internal/some/soem_backend.hpp"

#include "duatic_ethercat_interface/exceptions.hpp"

#include <soem_vendor/ethercat.h>

namespace duatic::ethercat_interface::internal::some
{

SOEMBackend::SOEMBackend(const std::string& interface) : interface_name_(interface)
{
}

void SOEMBackend::add_device(std::shared_ptr<EthercatDeviceBase> d)
{
  devices_.push_back(d);
}

int SOEMBackend::initialize()
{
  if (ecx_init(&context_.context, interface_name_.c_str()) <= 0) {
    throw BackendError("Failed to open interface: " + interface_name_ + " Run as root!");
  }

  const int device_count = ecx_config_init(&context_.context, FALSE);
  if (device_count <= 0) {
    throw BackendError("No devices found on the bus");
  }
  return device_count;
}

void SOEMBackend::scan()
{
}

void SOEMBackend::configure()
{
  // SOEM-specific PDO setup
}

void SOEMBackend::activate()
{
}

void SOEMBackend::cycle()
{
}

}  // namespace duatic::ethercat_interface::internal::some
