#include "duatic_ethercat_interface/ethercat_bus.hpp"
#include "duatic_ethercat_interface/internal/some/soem_backend.hpp"

namespace duatic::ethercat_interface
{

using namespace internal;

struct EthercatBus::Impl
{
  // This is a double indirection but it is hard to get around that if we want to hide the implementation
  std::unique_ptr<EthercatBackend> backend_;

  Impl(std::unique_ptr<EthercatBackend> backend) : backend_(std::move(backend))
  {
  }
};

EthercatBus::EthercatBus(const std::string& interface, const Backend backend)
{
  switch (backend) {
    case Backend::SOEM:
      impl_ = std::make_unique<Impl>(std::make_unique<some::SOEMBackend>(interface));
      break;
  }
}
}  // namespace duatic::ethercat_interface
