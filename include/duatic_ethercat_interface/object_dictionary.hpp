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
  explicit ObjectDictionary(const std::vector<SDOEntry> entries)
  {
    for (const auto& e : entries) {
      sdo_entries_[e.index] = e;
    }
  }

  bool has_index(const SDOIndex index) const
  {
    return sdo_entries_.contains(index);
  }

  const SDOEntry& at(const SDOIndex index) const
  {
    return sdo_entries_.at(index);
  }
  const SDOEntry& at(const std::string name)
  {
    const auto it = std::find_if(sdo_entries_.begin(), sdo_entries_.end(),
                                 [name](const auto& elem) { return elem.second.name == name; });

    if (it == sdo_entries_.end()) {
      throw std::runtime_error("Element not found");
    }
    return it->second;
  }

  const auto& entries() const
  {
    return sdo_entries_;
  }

private:
  std::unordered_map<SDOIndex, SDOEntry> sdo_entries_;
};

}  // namespace duatic::ethercat_interface
