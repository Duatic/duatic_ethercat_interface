#pragma once

#include "duatic_ethercat_interface/ethercat_bus.hpp"

namespace duatic::ethercat_interface::internal
{
class EthercatBackend
{
public:
  virtual ~EthercatBackend() = default;

  virtual void add_device(std::shared_ptr<EthercatDeviceBase>) = 0;

  virtual int initialize() = 0;
  virtual void scan() = 0;
  virtual void configure() = 0;
  virtual void activate() = 0;
  virtual void cycle() = 0;
};
}  // namespace duatic::ethercat_interface::internal
