#pragma once

#include <stdexcept>

namespace duatic::ethercat_interface
{
class BackendError : public std::runtime_error
{
public:
  explicit BackendError(const std::string& msg) : std::runtime_error(msg)
  {
  }
};
}  // namespace duatic::ethercat_interface
