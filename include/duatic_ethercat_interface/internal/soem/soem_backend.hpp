#pragma once

#include <vector>
#include <memory>

#include "duatic_ethercat_interface/internal/ethercat_backend.hpp"

#include "duatic_ethercat_interface/internal/some/soem_context.hpp"

namespace duatic::ethercat_interface::internal::some
{
class SOEMBackend : public EthercatBackend
{
public:
  SOEMBackend(const std::string& interface);
  void add_device(std::shared_ptr<EthercatDeviceBase> d) override;

  int initialize() override;

  void scan() override;

  void configure() override;

  void activate() override;

  void cycle() override;

private:
  std::vector<std::shared_ptr<EthercatDeviceBase>> devices_;
  EthercatContext context_;
  const std::string interface_name_;
};
}  // namespace duatic::ethercat_interface::internal::some
