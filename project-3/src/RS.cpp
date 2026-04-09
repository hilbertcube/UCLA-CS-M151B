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
#include "RS.h"

using namespace tinyrv;

ReservationStation::ReservationStation(uint32_t size)
  : store_(size)
  , indices_(size)
  , next_index_(0) {
  for (uint32_t i = 0; i < size; ++i) {
    store_[i].valid = false;
    indices_[i] = i;
  }
}

ReservationStation::~ReservationStation() {}

void ReservationStation::issue(uint32_t rob_tag, uint32_t rs1_rob, uint32_t rs2_rob, uint32_t rs1_data, uint32_t rs2_data, Instr::Ptr instr) {
  assert(!this->full());
  uint32_t index = indices_[next_index_++];
  store_[index] = {true, rob_tag, rs1_rob, rs2_rob, rs1_data, rs2_data, instr};
}

void ReservationStation::release(uint32_t index) {
  auto& entry = store_.at(index);
  if (!entry.valid)
    return;
  entry.valid = false;
  indices_[--next_index_] = index;
}

void ReservationStation::invalidate(uint32_t rob_tag) {
  for (uint32_t i = 0; i < store_.size(); ++i) {
    auto& entry = store_[i];
    if (!entry.valid)
      continue;
    if (is_rob_younger(entry.rd_rob, rob_tag)) {
      entry.valid = false;
    }
  }

  uint32_t active = 0;
  for (uint32_t i = 0; i < store_.size(); ++i) {
    if (store_[i].valid) {
      indices_[active++] = i;
    }
  }
  for (uint32_t i = 0; i < store_.size(); ++i) {
    if (!store_[i].valid) {
      indices_[active++] = i;
    }
  }
  next_index_ = 0;
  for (uint32_t i = 0; i < store_.size(); ++i) {
    if (store_[i].valid) {
      ++next_index_;
    }
  }
}

void ReservationStation::dump() const {
  for (uint32_t i = 0; i < store_.size(); ++i) {
    auto& entry = store_[i];
    if (entry.valid) {
      if (entry.instr->getFUType() == FUType::LSU) {
        DT(4, "RS[" << i << "] rob=" << entry.rd_rob << ", rs1=" << entry.rs1_rob << ", rs2=" << entry.rs2_rob << " (#" << entry.instr->getId() << ")");
      } else {
        DT(4, "RS[" << i << "] rob=" << entry.rd_rob << ", rs1=" << entry.rs1_rob << ", rs2=" << entry.rs2_rob << " (#" << entry.instr->getId() << ")");
      }
    }
  }
}