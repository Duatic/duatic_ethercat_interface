/*
 * Copyright 2026 Duatic AG
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
