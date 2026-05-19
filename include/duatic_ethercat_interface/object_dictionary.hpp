#pragma once

#include <string>
#include <vector>
#include <unordered_map>

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
  explicit ObjectDictionary(const std::vector<SDOEntry>& entries) : sdo_entries_(entries)
  {
    for (const auto& e : sdo_entries_) {
      sdo_entries_by_index_[e.index] = &e;
      sdo_entries_by_name_[e.name] = &e;
    }
  }

  bool has_index(const SDOIndex index) const
  {
    return sdo_entries_by_index_.contains(index);
  }

  const SDOEntry& at(const SDOIndex index) const
  {
    return *sdo_entries_by_index_.at(index);
  }
  const SDOEntry& at(const std::string name)
  {
    return *sdo_entries_by_name_.at(name);
  }

  const auto& entries() const
  {
    return sdo_entries_;
  }

private:
  std::vector<SDOEntry> sdo_entries_;
  std::unordered_map<SDOIndex, const SDOEntry*> sdo_entries_by_index_;
  std::unordered_map<std::string, const SDOEntry*> sdo_entries_by_name_;
};

}  // namespace duatic::ethercat_interface
