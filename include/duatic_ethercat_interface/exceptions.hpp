#pragma once

#include <stdexcept>
#include <optional>

#include "duatic_ethercat_interface/backend.hpp"

namespace duatic::ethercat_interface
{
/**
 * @brief BackendError - Exception which represents and error which comes for a specific backend
 */
class BackendError : public std::runtime_error
{
public:
  explicit BackendError(const std::string& msg, Backend backend = Backend::Unknown,
                        std::optional<int> ec = std::nullopt)
    : std::runtime_error(msg), backend_(backend), ec_(ec)
  {
    full_msg_ = "[" + to_string(backend) + "] " + msg;
    if (ec) {
      full_msg_ += " (ec: " + std::to_string(ec.value()) + ")";
    }
  }
  const char* what() const noexcept override
  {
    return full_msg_.c_str();
  }
  /**
   * @brief get_backend - obtain the backend identifier of the used backend
   */
  Backend get_backend() const noexcept
  {
    return backend_;
  }
  /**
   * @brief get_error_code - obtain the (backend specific) error code of the specific implementation
   */
  std::optional<int> get_error_code() const noexcept
  {
    return ec_;
  }

private:
  Backend backend_{ Backend::Unknown };
  std::optional<int> ec_{};

  std::string full_msg_;
};

class DeviceNotFound : public BackendError
{
public:
  explicit DeviceNotFound(const std::string& msg, Backend backend = Backend::Unknown,
                          std::optional<int> ec = std::nullopt)
    : BackendError(msg, backend, ec)
  {
  }
};

class DeviceConfigurationError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
}  // namespace duatic::ethercat_interface
