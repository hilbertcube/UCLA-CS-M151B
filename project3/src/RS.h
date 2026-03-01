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
#include "instr.h"
#include "CDB.h"
#include "types.h"

namespace tinyrv {

// ReservationStation tracks dependencies via ROB indices directly (rs1_rob, rs2_rob),
// eliminating the need for a separate Register Status Table (RST).
// This simplifies dependency tracking while maintaining correct OoO execution.

class ReservationStation {
public:

  struct rs_entry_t {
    bool     valid;   // valid entry
    uint32_t rd_rob;  // allocated ROB index
    uint32_t rs1_rob; // ROB index producing rs1 (-1 indicates data is already available)
    uint32_t rs2_rob; // ROB index producing rs2 (-1 indicates data is already available)
    uint32_t rs1_data;// rs1 data
    uint32_t rs2_data;// rs2 data
    Instr::Ptr instr; // instruction data

    void update_operands(const CDBResult& result) {
      // update operands if this RS entry is waiting for them
      // set a resolved operand ROB to INVALID_ROB
      // TODO;
    }

    bool operands_ready() const {
      // operands are ready when there are no pending ROB producers for rs1 and rs2
      // TOOD:
    }
  };

  ReservationStation(uint32_t size);

  ~ReservationStation();

  bool operands_ready(uint32_t index) const {
    return store_.at(index).operands_ready();
  }

  const rs_entry_t& get_entry(uint32_t index) const {
    return store_.at(index);
  }

  rs_entry_t& get_entry(uint32_t index) {
    return store_.at(index);
  }

  void issue(uint32_t rob_tag, uint32_t rs1_rob, uint32_t rs2_rob, uint32_t rs1_data, uint32_t rs2_data, Instr::Ptr instr);

  void release(uint32_t index);

  void invalidate(uint32_t rob_tag);

  bool full() const {
    return (next_index_ == store_.size());
  }

  bool empty() const {
    return (next_index_ == 0);
  }

  uint32_t size() const {
    return store_.size();
  }

  void dump() const;

private:

  std::vector<rs_entry_t> store_;
  std::vector<uint32_t> indices_;
  uint32_t next_index_;
};

}