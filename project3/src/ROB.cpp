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

#include <iostream>
#include <assert.h>
#include <util.h>
#include "types.h"
#include "debug.h"
#include "ROB.h"

using namespace tinyrv;

ReorderBuffer::ReorderBuffer(uint32_t size)
  : store_(size)
  , head_index_(0)
  , tail_index_(0)
  , count_(0)
  , phase_bit_(0)
  , index_mask_(size - 1)
{
  assert(size && ((size & (size - 1)) == 0));
  for (auto& entry : store_) {
    entry.valid = false;
    entry.ready = false;
  }
}

ReorderBuffer::~ReorderBuffer() {
  //--
}

uint32_t ReorderBuffer::allocate(Instr::Ptr instr) {
  assert(!this->full());
  uint32_t index = tail_index_;
  auto size = store_.size();
  auto mask = size - 1;
  uint32_t tag = (phase_bit_ * size) | (index & mask);
  store_[index] = {true, false, 0, tag, instr};
  // Compute tag: [phase_bit | index]
  tail_index_ = (index + 1) & mask;
  if (tail_index_ == 0) {
    phase_bit_ ^= 0x1; // flip phase on wrap
  }
  ++count_;
  return tag;
}

void ReorderBuffer::update(const CDBResult& result) {
  auto index = result.rob_tag & index_mask_;
  auto& entry = store_[index];
  // Stale CDB broadcasts can occur across flush boundaries.
  // Ignore updates that no longer map to a live ROB entry.
  if (!entry.valid || entry.ready)
    return;

  // Udate the ROB entry
  // TODO:

  if (entry.instr->getExeFlags().use_rd) {
    DT(2, "Writeback: value=0x" << std::hex << result.value << std::dec << ", " << *entry.instr);
  } else {
    DT(2, "Writeback: " << *entry.instr);
  }
}

void ReorderBuffer::invalidate(uint32_t rob_tag) {
  if (this->empty())
    return;

  auto size = store_.size();
  auto mask = size - 1;
  auto size2 = size * 2;
  auto mask2 = size2 - 1;

  uint32_t old_tail_tag = (phase_bit_ * size) | tail_index_;
  uint32_t new_tail_tag = (rob_tag + 1) & mask2;
  uint32_t removed = (old_tail_tag - new_tail_tag) & mask2;
  if (removed == 0)
    return;

  for (uint32_t i = 0; i < removed; ++i) {
    auto tag = (new_tail_tag + i) & mask2;
    auto index = tag & mask;
    store_[index].valid = false;
    store_[index].ready = false;
  }

  assert(removed <= count_);
  count_ -= removed;
  tail_index_ = new_tail_tag & mask;
  phase_bit_ = (new_tail_tag / size) & 0x1;
}

uint32_t ReorderBuffer::pop() {
  assert(!this->empty() && store_[head_index_].valid && store_[head_index_].ready);
  store_[head_index_].valid = false;
  store_[head_index_].ready = false;
  head_index_ = (head_index_ + 1) % store_.size();
  --count_;
  return head_index_;
}

void ReorderBuffer::dump() {
  for (uint32_t i = 0; i < store_.size(); ++i) {
    auto& entry = store_[i];
    if (entry.valid) {
      DT(4, "ROB[" << i << "] ready=" << entry.ready << ", head=" << (i == head_index_) << " (#" << entry.instr->getId() << ")");
    }
  }
}