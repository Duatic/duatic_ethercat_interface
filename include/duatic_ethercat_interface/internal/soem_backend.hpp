#pragma once

#include <vector>
#include <memory>

#include "duatic_ethercat_interface/internal/ethercat_backend.hpp"

namespace duatic::ethercat_interface::internal
{
class SOEMBusImpl : public EthercatBackend
{
public:
  void add_device(std::shared_ptr<EthercatDeviceBase> d) override;

  void init() override;

  void scan() override;

  void configure() override;

  void activate() override;

  void cycle() override;

private:
  std::vector<std::shared_ptr<EthercatDeviceBase>> devices;
};
}  // namespace duatic::ethercat_interface::internal
