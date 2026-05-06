#pragma once

#include <string>

namespace duatic::ethercat_interface
{

enum class Backend
{
  Unknown,
  SOEM,
  Etherlab
};

inline std::string to_string(const Backend& b)
{
  switch (b) {
    case Backend::Unknown:
      return "Unknown";
    case Backend::SOEM:
      return "SOEM";
    case Backend::Etherlab:
      return "Etherlab";
  }
  return "Unknown";
}

}  // namespace duatic::ethercat_interface
