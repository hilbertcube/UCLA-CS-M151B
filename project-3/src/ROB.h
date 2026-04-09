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
#include "instr.h"
#include "CDB.h"

namespace tinyrv {

class ReorderBuffer {
public:

  struct rob_entry_t {
    bool       valid;   // valid entry
    bool       ready;   // completed
    uint32_t   result;  // result data
    uint32_t   tag;     // ROB tag (phase|index)
    Instr::Ptr instr;   // instruction data
  };

  ReorderBuffer( uint32_t size);

  ~ReorderBuffer();

  bool full() const {
    return count_ == store_.size();
  }

  bool empty() const {
    return count_ == 0;
  }

  uint32_t allocate(Instr::Ptr instr);

  uint32_t pop();

  void update(const CDBResult& result);

  void invalidate(uint32_t rob_tag);

  uint32_t head_tag() const {
    return store_.at(head_index_).tag;
  }

  uint32_t head_index() const {
    return head_index_;
  }

  const rob_entry_t& get_entry(uint32_t index) const {
    return store_.at(index & index_mask_);
  }

  rob_entry_t& get_entry(uint32_t index) {
    return store_.at(index & index_mask_);
  }

  void dump();

private:

  std::vector<rob_entry_t> store_;
  uint32_t head_index_;
  uint32_t tail_index_;
  uint32_t count_;
  uint32_t phase_bit_;
  uint32_t index_mask_;

};

}