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
#include "FU.h"
#include "core.h"
#include "debug.h"

using namespace tinyrv;



static uint32_t execute_br_op(BrOp br_op, uint32_t rs1_data, uint32_t rs2_data) {
  bool br_taken = false;

  switch (br_op) {
  case BrOp::NONE:
    break;
  case BrOp::JAL:
  case BrOp::JALR: {
    br_taken = true;
    break;
  }
  case BrOp::BEQ: {
    br_taken = (rs1_data == rs2_data);
    break;
  }
  case BrOp::BNE: {
    br_taken = (rs1_data != rs2_data);
    break;
  }
  case BrOp::BLT: {
    br_taken = ((int32_t)rs1_data < (int32_t)rs2_data);
    break;
  }
  case BrOp::BGE: {
    br_taken = ((int32_t)rs1_data >= (int32_t)rs2_data);
    break;
  }
  case BrOp::BLTU: {
    br_taken = (rs1_data < rs2_data);
    break;
  }
  case BrOp::BGEU: {
    br_taken = (rs1_data >= rs2_data);
    break;
  }
  default:
    std::abort();
  }

  return br_taken;
}

void BRU::tick() {
  if (!input_.valid)
    return; // no instruction to execute
  if (++cycles_ < BRU_LATENCY)
    return; // wait until operation is complete
  if (output_.valid)
    return; // wait until output is consumed

  auto br_op = input_.instr->getBrOp();
  uint32_t next_pc = input_.instr->getPC() + 4;

  bool br_taken;
  if (br_op == BrOp::JAL || br_op == BrOp::JALR) {
    br_taken = true;   // unconditional jump
  } else {
    // calculate branch condition
    br_taken = execute_br_op(br_op, input_.rs1_value, input_.rs2_value);
  }

  uint32_t br_target;
  if (br_taken) {
    // calculate branch target
    br_target = execute_alu_op(*input_.instr, input_.rs1_value, input_.rs2_value);
  } else {
    br_target = next_pc;
  }

  if (br_op != BrOp::JAL && br_op != BrOp::JALR && br_op != BrOp::NONE) {
    DT(3, "BRU: op=" << br_op << ", rs1=0x" << std::hex << input_.rs1_value << ", rs2=0x" << input_.rs2_value
       << ", taken=" << std::dec << br_taken << ", target=0x" << std::hex << br_target << std::dec
       << " (#" << input_.instr->getId() << ")");
  }

  core_->update_branch(br_target, br_taken, input_.instr, input_.rob_tag);

  // send result
  output_.result = CDBResult(next_pc, input_.rob_tag);
  output_.valid = true;

  // consume input
  input_.valid = false;
}
