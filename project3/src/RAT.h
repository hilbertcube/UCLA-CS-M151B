// Copyright 2024 blaise
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <vector>
#include <cassert>

namespace tinyrv {

class RegisterAliasTable {
public:
  using mapping_t = std::pair<bool, uint32_t>;
  using store_t = std::vector<mapping_t>;

  RegisterAliasTable(uint32_t size) : store_(size) {
    for (auto& entry : store_) {
      entry = {false, 0};
    }
  }

  ~RegisterAliasTable() {}

  bool exists(uint32_t index) const {
    return store_.at(index).first;
  }

  int get(uint32_t index) const {
    assert(store_[index].first);
    return store_[index].second;
  }

  void set(uint32_t index, uint32_t value) {
    store_.at(index) = {true, value};
  }

  void clear(uint32_t index) {
    store_.at(index) = {false, 0};
  }

  bool clear_mapping(uint32_t index, uint32_t value) {
    auto& mapping = store_.at(index);
    if (mapping.first && mapping.second == value) {
      mapping = {false, 0};
      return true;
    }
    return false;
  }

private:
  store_t store_;
};

}