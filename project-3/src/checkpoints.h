// Copyright 2024 Blaise Tine
//
// Licensed under the Apache License;
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <vector>
#include <cstdint>
#include "types.h"
#include "RAT.h"

namespace tinyrv {

class Checkpoints {
public:
  Checkpoints(uint32_t size = CHECKPOINT_SIZE)
    : store_(size)
  {}

  void reset() {
    store_.assign(store_.size(), CheckpointEntry());
  }

  int find(uint32_t rob_tag) const {
    for (uint32_t i = 0; i < store_.size(); ++i) {
      auto& cp = store_[i];
      if (cp.valid && cp.rob_tag == rob_tag) {
        return i;
      }
    }
    return -1;
  }

  int alloc() const {
    for (uint32_t i = 0; i < store_.size(); ++i) {
      if (!store_[i].valid) {
        return i;
      }
    }
    return -1;
  }

  bool save(uint32_t rob_tag, const RegisterAliasTable& rat) {
    auto index = this->alloc();
    if (index < 0)
      return false;
    store_[index].valid = true;
    store_[index].rob_tag = rob_tag;
    store_[index].rat = rat;
    return true;
  }

  bool release(uint32_t rob_tag) {
    auto index = this->find(rob_tag);
    if (index < 0)
      return false;
    store_[index].valid = false;
    return true;
  }

  void restore(uint32_t rob_tag, RegisterAliasTable* out) const {
    auto index = this->find(rob_tag);
    if (index >= 0) {
      *out = store_[index].rat;
    }
  }

  void clear_RAT_mapping(uint32_t reg_index, uint32_t rob_tag) {
    for (auto& cp : store_) {
      if (!cp.valid)
        continue;
      cp.rat.clear_mapping(reg_index, rob_tag);
    }
  }

  void invalidate(uint32_t rob_tag) {
    for (auto& cp : store_) {
      if (!cp.valid)
        continue;
      if (cp.rob_tag == rob_tag || is_rob_younger(cp.rob_tag, rob_tag)) {
        cp.valid = false;
      }
    }
  }

private:
  struct CheckpointEntry {
    bool valid;
    RegisterAliasTable rat;
    uint32_t rob_tag;

    CheckpointEntry()
      : valid(false)
      , rat(NUM_REGS)
      , rob_tag(INVALID_ROB)
    {}
  };

  std::vector<CheckpointEntry> store_;
};

}
