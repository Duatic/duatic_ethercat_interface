#pragma once

#include <string>
#include "duatic_ethercat_interface/types.hpp"

namespace duatic::ethercat_interface
{
struct SDOSubEntry
{
  SDOSubIndex index;
  std::string name;
  DataType data_type;
  std::size_t size;
};
struct SDOEntry
{
  SDOIndex index;
  SDOObjectCode obj_type;
  DataType data_type;
  std::size_t count_sub_indices;
  std::string name;

  std::vector<SDOSubEntry> sub_entries;
};

class ObjectDictionary
{
public:
  explicit ObjectDictionary(const std::vector<SDOEntry> entries) : sdo_entries_(entries)
  {
  }

private:
  std::vector<SDOEntry> sdo_entries_;
};

}  // namespace duatic::ethercat_interface