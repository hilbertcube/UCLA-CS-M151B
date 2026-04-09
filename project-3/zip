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

#include <assert.h>
#include <util.h>
#include "FU.h"
#include "core.h"

using namespace tinyrv;

namespace tinyrv {

uint32_t execute_alu_op(const Instr &instr, uint32_t rs1_data, uint32_t rs2_data) {
  auto exe_flags  = instr.getExeFlags();
  auto alu_op     = instr.getAluOp();

  uint32_t alu_s1 = exe_flags.alu_s1_PC ? instr.getPC() : (exe_flags.alu_s1_rs1 ? instr.getRs1() :  rs1_data);
  uint32_t alu_s2 = exe_flags.alu_s2_imm ? instr.getImm() : rs2_data;

  if (exe_flags.alu_s1_inv) {
    alu_s1 = ~alu_s1;
  }

  uint32_t rd_data = 0;

  switch (alu_op) {
  case AluOp::NONE:
    break;
  case AluOp::ADD: {
    rd_data = alu_s1 + alu_s2;
    break;
  }
  case AluOp::SUB: {
    rd_data = alu_s1 - alu_s2;
    break;
  }
  case AluOp::AND: {
    rd_data = alu_s1 & alu_s2;
    break;
  }
  case AluOp::OR: {
    rd_data = alu_s1 | alu_s2;
    break;
  }
  case AluOp::XOR: {
    rd_data = alu_s1 ^ alu_s2;
    break;
  }
  case AluOp::SLL: {
    rd_data = alu_s1 << alu_s2;
    break;
  }
  case AluOp::SRL: {
    rd_data = alu_s1 >> alu_s2;
    break;
  }
  case AluOp::SRA: {
    rd_data = (int32_t)alu_s1 >> alu_s2;
    break;
  }
  case AluOp::LTI: {
    rd_data = (int32_t)alu_s1 < (int32_t)alu_s2;
    break;
  }
  case AluOp::LTU: {
    rd_data = alu_s1 < alu_s2;
    break;
  }
  default:
    std::abort();
  }

  return rd_data;
}

}

void ALU::tick() {
  if (!input_.valid)
    return; // no instruction to execute
  if (++cycles_ < ALU_LATENCY)
    return; // wait until operation is complete
  if (output_.valid)
    return; // wait until output is consumed

  auto result = execute_alu_op(*input_.instr, input_.rs1_value, input_.rs2_value);

  // send result
  output_.result = CDBResult(result, input_.rob_tag);
  output_.valid = true;

  // consume input
  input_.valid = false;
}