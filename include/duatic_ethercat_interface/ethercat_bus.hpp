#pragma once

#include <memory>
#include <vector>

#include "duatic_ethercat_interface/ethercat_device.hpp"

namespace duatic::ethercat_interface
{

enum class Backend
{
  SOEM,
  Etherlab
};

class EthercatBus
{
public:
  explicit EthercatBus(const std::string& interface, const Backend backend);

  void initialize();

protected:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace duatic::ethercat_interface
