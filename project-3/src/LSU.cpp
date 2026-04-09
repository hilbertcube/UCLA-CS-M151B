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

using namespace tinyrv;

namespace {
inline uint32_t AGU_execute(const Instr& instr, uint32_t base_addr) {
  return base_addr + instr.getImm();
}
}

void LSU::tick() {
  auto dcache = core_->get_dcache();

  // Decoupled load response handling
  if (!dcache->core_rsp.empty() && !output_.valid) {
    auto rsp = dcache->core_rsp.peek();
    auto& entry = core_->LSQ_->get_entry(rsp.user);

    // format returned value
    auto func3 = static_cast<MemDataType>(entry.instr->getFunc3());
    uint32_t data_bytes = 1 << ((int)func3 & 0x3);
    uint32_t data_width = 8 * data_bytes;
    uint32_t result = 0;
    switch (func3) {
    case MemDataType::LB:
    case MemDataType::LH:
    case MemDataType::LW:
      result = sext(rsp.data, data_width);
      break;
    case MemDataType::LBU:
      result = rsp.data & 0xff;
      break;
    case MemDataType::LHU:
      result = rsp.data & 0xffff;
      break;
    default:
      std::abort();
    }

    // send data to LSQ
    if (core_->LSQ_->update_data(rsp.user, result)) {
      // send result to CDB
      output_.valid = true;
      output_.result = CDBResult(result, entry.rob_tag);
    }

    // consume response
    dcache->core_rsp.pop();
    return;
  }

  // process issued load/store instructions
  if (!input_.valid)
    return; // no instruction to execute
  if (++cycles_ < LSU_LATENCY)
    return; // wait until operation is complete
  if (output_.valid)
    return; // wait until output is consumed

  auto instr = input_.instr;
  auto exe_flags = instr->getExeFlags();
  auto lsq_idx = instr->getMetaData().lsu.lsq_idx;

  // compute effective address
  uint32_t addr = AGU_execute(*instr, input_.rs1_value);
  core_->LSQ_->update_address(lsq_idx, addr);

  if (exe_flags.is_store) {
    core_->LSQ_->update_data(lsq_idx, input_.rs2_value);
    // send store to writeback stage
    output_.result = CDBResult(0, input_.rob_tag);
    output_.valid = true;
  }

  input_.valid = false;
}

bool LSU::issue_load(uint32_t addr, uint32_t fwd_data, uint32_t lsq_idx, bool forwarded) {
  if (forwarded) {
    if (output_.valid)
      return false;

    if (!core_->LSQ_->update_data(lsq_idx, fwd_data))
      return true;

    auto& entry = core_->LSQ_->get_entry(lsq_idx);
    output_.result = CDBResult(fwd_data, entry.rob_tag);
    output_.valid = true;
    return true;
  }

  auto dcache = core_->get_dcache();
  cache_req_t req{false, addr, 0, 0, lsq_idx};
  return dcache->core_req.try_send(req, 0);
}

bool LSU::issue_store(uint32_t addr, uint32_t data, uint32_t lsq_idx) {
  auto& entry = core_->LSQ_->get_entry(lsq_idx);
  auto func3 = static_cast<MemDataType>(entry.instr->getFunc3());

  uint64_t byteen = 0xf;
  switch (func3) {
  case MemDataType::SB:
    byteen = 0x1;
    break;
  case MemDataType::SH:
    byteen = 0x3;
    break;
  case MemDataType::SW:
    byteen = 0xf;
    break;
  default:
    std::abort();
  }

  if (addr >= uint64_t(IO_COUT_ADDR)
   && addr < (uint64_t(IO_COUT_ADDR) + IO_COUT_SIZE)) {
    core_->writeToStdOut(&data);
    return true;
  }

  auto dcache = core_->get_dcache();
  cache_req_t req{true, addr, byteen, data, 0};
  return dcache->core_req.try_send(req, 0);
}